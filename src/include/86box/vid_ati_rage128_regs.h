/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- register and identity constants.
 *
 *          Register offsets, field masks and reset values for every block
 *          of the device, plus the board identity (PCI ids, video BIOS
 *          images, reference clock).
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 VR / RAGE 128 GL Graphics
 *              Controller Specifications", GCS-C04100 Rev 0.05, November
 *              1998 (the board straps). Cited as "GCS: ...".
 *
 *          [3] ATI Technologies, "RAGE 128 VR/GL Register Reference
 *              Manual", RRG-G04100-C Rev 0.02, January 1999. Cited as
 *              "VR/GL RRM: ...".
 *
 *          [4] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999. Cited as "SDK: ...".
 *
 *          [5] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Bus Master Registers", 1999. Cited as "Bus Master
 *              supplement" with its printed page (BM-n).
 *
 *          [6] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Multimedia Registers", 1999. Cited as "Multimedia
 *              supplement" with the register name; it has no page numbers.
 *
 *          [7] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Registers for CCE 3D Packets", 1999. Cited as "CCE 3D
 *              supplement" with the register name; it has no page numbers.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEO_ATI_RAGE128_REGS_H
#define VIDEO_ATI_RAGE128_REGS_H
/* clang-format off */

/* PCI identity. The AGP board is a Rage 128 Pro GL, device 0x5046; the
   PCI board is a Rage 128 GL, device 0x5245 (PCI_CHIP_RAGE128PF and
   PCI_CHIP_RAGE128RE, xf86-video-r128 r128_probe.h; named "Pro GL PF
   (AGP)" and "GL RE (PCI)" in r128_probe.c R128Chipsets).

   The subsystem vendor and id are not constants. After PCI reset the
   chip reads them from bytes 0x70-0x73 of the BIOS ROM (GCS: Standard
   Boot-up Sequence, p. 4-29 / PDF 77; table in GCS: ROM Based Straps,
   p. 4-35 / PDF 83), so each board reports what its ROM image holds:
   1002:0018 for the AGP image, 1002:0008 for the XPERT 128 PCI image,
   1002:2000 for the MAXX image. ATI's Windows 98 (Atii9xaa.inf
   4.13.7192) and Windows 2000 (ATII2KAA.INF 5.13.3279) drivers list
   the first two, as "Rage Fury Pro/Xpert 2000 Pro" and "Xpert 128". */
#define RAGE128_PCI_VENDOR      0x1002
#define RAGE128_PCI_DEVICE_PF   0x5046
#define RAGE128_PCI_DEVICE_RE   0x5245
#define RAGE128_ROM_STRAP_SUBSYS 0x70 /* ROM bytes 0x70-0x73: subsys vendor, subsys id, little-endian */

/* PCI capability chain on the AGP board: CAP_PTR (config 0x34) points to
   the AGP capability at 0x50, whose NEXT_PTR points to the power
   management capability at 0x5c, which ends the list. The guide fixes
   the two capability blocks and their link
   (RRG: CAPABILITIES_ID, p. 3-9 / PDF 27; RRG: PMI_NXT_CAP_PTR,
   p. 3-189 / PDF 207) but gives CAP_PTR a default of 0
   (RRG: CAPABILITIES_PTR, p. 3-8 / PDF 26). The lspci listing of a
   Rage 128 PRO AGP board in Debian bug report 758934 shows the list at
   0x50 and 0x5c, so CAP_PTR reads 0x50. The PCI board lists only the
   power management block (lspci of a Rage 128 PRO PCI board, Ubuntu bug
   report 1664531), so there CAP_PTR reads 0x5c and the AGP block reads
   0. The register values are the guide's per-field defaults
   (RRG: AGP_STATUS and AGP_COMMAND, p. 3-185 / PDF 203; RRG:
   PMI_PMC_REG, p. 3-189 / PDF 207; RRG: PMI_PMCSR_REG, p. 3-190 /
   PDF 208). */
#define RAGE128_PCI_CAP_PTR        0x50
#define RAGE128_AGP_CAP_ID         0x02       /* AGP capability             */
#define RAGE128_AGP_NEXT_PTR       0x5c       /* -> PMI capability          */
#define RAGE128_AGP_REV            0x20       /* AGP major 2, minor 0       */
#define RAGE128_AGP_STATUS         0x1f000207 /* RQ 0x1f, SBA, 4x/2x/1x     */
#define RAGE128_AGP_COMMAND_SBA_EN 0x00000200 /* SBA_EN bit reads 1 (RO)    */
#define RAGE128_AGP_COMMAND_MASK   0xff000107 /* RQ_DEPTH, AGP_EN, DATA_RATE */
#define RAGE128_PMI_CAP_ID         0x01       /* PCI power management       */
#define RAGE128_PMI_PMC            0x0202     /* PMI 1.1, D1 supported      */
#define RAGE128_PMI_POWER_STATE_D2 0x02       /* unsupported, write dropped */

/* BARs: 0 = linear framebuffer aperture, 1 = I/O register block, 2 =
   memory-mapped registers (RRG: MEM_BASE, IO_BASE and REG_BASE,
   pp. 3-6-3-7 / PDF 24-25). The X driver reads the framebuffer from
   BAR0 and the registers from BAR2 (xf86-video-r128 r128_driver.c
   R128PreInitConfig). */
#define RAGE128_LFB_SIZE  0x04000000 /* BAR0: 64 MB whatever the VRAM size.
                                        MEM_BASE decodes only bits [31:26],
                                        and the X driver masks the BAR0
                                        base with 0xfc000000, so the BAR
                                        must be 64 MB sized and aligned.
                                        The lower 32 MB is local VRAM, the
                                        upper 32 MB the AGP window (RRG:
                                        DST_OFFSET, p. 3-137 / PDF 155), and
                                        CONFIG_APER_SIZE reads back half of
                                        BAR0. */
#define RAGE128_IO_SIZE   0x00000100 /* BAR1: 256-byte I/O block (IO_BASE
                                        decodes bits [31:8]) */
#define RAGE128_MMIO_SIZE 0x00004000 /* BAR2: 16 KB (REG_BASE decodes bits
                                        [31:14]); the device puts both 8 KB
                                        register apertures in it */

/* Video BIOS images, one per board. Both are copies of a shadowed BIOS
   taken after POST, so the three words at 0x126-0x12b still hold what
   that POST stored there: the card's PCI bus/device/function, its ROM
   segment and its I/O base. In the shipped copies those words are zero
   and the checksum byte is adjusted to match.
     AGP: 48 KB image (original SHA-256 8b6aae0e...), checksum byte at
          0xbfff.
     PCI: "RAGE128 GL PCI P/N 113-57403-102", BK1.0.13 (original SHA-256
          91837fab...), a 36 KB file whose header declares 32 KB, so the
          checksum byte is at 0x7fff.
   With the words zero, the BIOS init routine takes its first-run path:
   it asks the PCI BIOS (INT 1Ah, AX=B109h) for BAR1, then writes its ROM
   segment and a locator signature into BIOS_1_SCRATCH at that I/O base,
   where its I/O-base locator finds them later (RE:
   Rage128progl_zerostate.VBI @c000:01be). */
#define RAGE128_ROM_PATH_AGP "roms/video/ati_rage128/Rage128progl_zerostate.VBI"
#define RAGE128_ROM_PATH_PCI "roms/video/ati_rage128/Rage128gl_xpert128pci_zerostate.VBI"
#define RAGE128_ROM_SIZE 0x00010000 /* 64 KB window; image padded with 0xff */

/* Reference oscillator (XTALIN) frequency, per board, from the PLL
   information block of each board's BIOS image. The ROM word at 0x48
   points to the BIOS header, the word at header + 0x30 to the PLL block.
   The block holds the reference frequency at +0x0e in 10 kHz units, the
   reference divider at +0x10 and the memory controller clock (xclk) at
   +0x08, the fields xf86-video-r128 r128_driver.c R128GetPLLParameters
   reads.
     AGP image: 2700 = 27.00 MHz, divider 60, xclk 12000 = 120 MHz.
     PCI image: 2950 = 29.50 MHz, divider 65, xclk 9000 = 90 MHz. */
#define RAGE128_REF_FREQ_AGP_HZ 27000000.0
#define RAGE128_REF_FREQ_PCI_HZ 29500000.0

/* ------------------------------------------------------------------ */
/* Register-file offsets. The non-GUI registers at 0x0000-0x00ff are
   reachable both in the memory-mapped aperture and through the I/O
   block; 0x0f00-0x0fff is a read-only copy of PCI configuration space
   (RRG: Description of Mapped Memory Apertures, Table 2-1, p. 2-5 /
   PDF 15). */
/* ------------------------------------------------------------------ */
#define RAGE128_MM_INDEX 0x0000
#define RAGE128_MM_DATA  0x0004
/* MM_INDEX: MM_APER [31] picks the aperture MM_ADDR points into, 0 =
   register aperture, 1 = linear aperture 0 (the framebuffer); MM_ADDR
   [26:0] is the address, bits 1:0 hardwired to zero (RRG: MM_INDEX,
   p. 3-191 / PDF 209). MM_DATA then reads or writes that location. */
#define RAGE128_MM_INDEX_MM_APER 0x80000000
#define RAGE128_MM_INDEX_MM_ADDR 0x07fffffc

/* Clock control index/data pair, the way into the PLL register file
   (RRG: CLOCK_CNTL_INDEX, p. 3-86 / PDF 104). PLL_WR_EN must be set for
   a CLOCK_CNTL_DATA write to reach the PLL register; PPLL_DIV_SEL picks
   which of PPLL_DIV_0-3 drives the pixel clock. */
#define RAGE128_CLOCK_CNTL_INDEX 0x0008
#define RAGE128_CLOCK_CNTL_DATA  0x000c
#define RAGE128_PLL_ADDR_MASK      0x1f       /* CLOCK_CNTL_INDEX[4:0]  */
#define RAGE128_PLL_WR_EN          (1 << 7)   /* CLOCK_CNTL_INDEX[7]    */
#define RAGE128_PPLL_DIV_SEL_SHIFT 8          /* CLOCK_CNTL_INDEX[9:8]  */

/* BIOS scratch: four 32-bit read/write registers, "Scratch memory for
   use by video BIOS", reset value 0 (RRG: BIOS_0_SCRATCH, p. 3-85 /
   PDF 103). The video BIOS keeps its ROM segment in the low word of
   BIOS_1_SCRATCH and a locator signature in the high word, and finds
   its own I/O base by reading the signature back, scanning the I/O
   space if the first guess fails (RE: Rage128progl_zerostate.VBI
   @c000:028e). */
#define RAGE128_BIOS_0_SCRATCH 0x0010
#define RAGE128_BIOS_1_SCRATCH 0x0014
#define RAGE128_BIOS_2_SCRATCH 0x0018
#define RAGE128_BIOS_3_SCRATCH 0x001c

/* Interrupt controller. GEN_INT_CNTL enables sources; GEN_INT_STATUS
   reads the latched events and clears them on a write of 1 (the _AK
   bits). A set status bit raises the interrupt only when its enable is
   set (RRG: GEN_INT_CNTL, pp. 3-192-3-193 / PDF 210-211; RRG:
   GEN_INT_STATUS, pp. 3-193-3-196 / PDF 211-214). How the device raises
   each source: VBLANK when vertical blank starts and VSYNC when vertical
   sync starts, a front porch later; VLINE from a one-shot timer each
   frame at the CRTC_VLINE line; SNAPSHOT on a MANUAL_SNAPSHOT_NOW write;
   GUI_IDLE_INT when the engine goes from busy to idle. The flat panel,
   I2C, bus master, MPP and VIP sources belong to blocks the device does
   not have, so their bits read 0. */
#define RAGE128_GEN_INT_CNTL    0x0040
#define RAGE128_GEN_INT_STATUS  0x0044
#define RAGE128_GIC_VBLANK_EN   (1u << 0)  /* CRTC_VBLANK_INT_EN */
#define RAGE128_GIS_VBLANK      (1u << 0)  /* CRTC_VBLANK_INT (R) / _AK (W1C) */
#define RAGE128_GIC_VLINE_EN    (1u << 1)  /* CRTC_VLINE_INT_EN */
#define RAGE128_GIS_VLINE       (1u << 1)  /* CRTC_VLINE_INT (R) / _AK (W1C) */
#define RAGE128_GIC_VSYNC_EN    (1u << 2)  /* CRTC_VSYNC_INT_EN */
#define RAGE128_GIS_VSYNC       (1u << 2)  /* CRTC_VSYNC_INT (R) / _AK (W1C) */
#define RAGE128_GIC_SNAPSHOT_EN (1u << 3)  /* SNAPSHOT_INT_EN */
#define RAGE128_GIS_SNAPSHOT    (1u << 3)  /* SNAPSHOT_INT (R) / _AK (W1C) */
#define RAGE128_GIC_GUI_IDLE_EN (1u << 19) /* GUI_IDLE_INT_EN */
#define RAGE128_GIS_GUI_IDLE    (1u << 19) /* GUI_IDLE_INT (R), reset value 1 / _AK (W1C) */
#define RAGE128_GIC_SUPPORTED   (RAGE128_GIC_VBLANK_EN | RAGE128_GIC_VLINE_EN \
                                 | RAGE128_GIC_VSYNC_EN | RAGE128_GIC_SNAPSHOT_EN \
                                 | RAGE128_GIC_GUI_IDLE_EN) /* modeled sources */
#define RAGE128_GIS_ACK_MASK    (RAGE128_GIS_VBLANK | RAGE128_GIS_VLINE \
                                 | RAGE128_GIS_VSYNC | RAGE128_GIS_SNAPSHOT \
                                 | RAGE128_GIS_GUI_IDLE)    /* W1C-able bits */

/* Bus control (RRG: BUS_CNTL, pp. 3-106-3-108 / PDF 124-126; RRG:
   BUS_CNTL1, pp. 3-191-3-192 / PDF 209-210). */
#define RAGE128_BUS_CNTL  0x0030
#define RAGE128_BUS_CNTL1 0x0034

/* VGA aperture banking: page selects for the two 32 KB halves of the
   legacy A0000 window. The guide gives the fields, WPS0/RPS0 [9:0] and
   WPS1/RPS1 [25:16], but no unit (RRG: MEM_VGA_WP_SEL, p. 3-110 /
   PDF 128). The video BIOS VBE set-window handler sets the unit: with
   CRTC_EXT_CNTL.VGA_ATI_LINEAR clear it writes {8N, 8N+4} for 64 KB
   window N, a unit of 8 KB; with it set it writes {2N, 2N+1}, a unit of
   32 KB (RE: Rage128progl_zerostate.VBI @c000:45a6). RAGE128_VGA_PAGE_SIZE
   is the 8 KB unit; the banking code multiplies it by 4 for the second
   case. */
#define RAGE128_MEM_VGA_WP_SEL 0x0038
#define RAGE128_MEM_VGA_RP_SEL 0x003c
#define RAGE128_VGA_PAGE_SIZE  0x2000 /* 8 KB */
#define RAGE128_BUS_CNTL_DEFAULT  0x880f4f41 /* the per-field reset values */
#define RAGE128_BUS_CNTL_WO_MASK  0x00000006 /* BUS_MSTR_RESET [1] and
                                                BUS_FLUSH_BUF [2] are
                                                write-only strobes */
/* BUS_MASTER_DIS [6]: 1 = the chip may not start bus-master cycles. It
   resets to 1 (RRG: BUS_CNTL, p. 3-106 / PDF 124), and drivers clear it
   before they start the CCE on a ring in host memory (linux r128 DRM
   r128_cce.c r128_cce_init_ring_buffer; xf86-video-r128 r128_dri.c
   R128DRICCEInit). */
#define RAGE128_BUS_CNTL_BUS_MASTER_DIS 0x00000040

/* GPIO_MONID: four general-purpose pads, used for the analog monitor's
   DDC2 (I2C) lines, which the driver bit-bangs. Each pad has an output
   bit in MONID_A [3:0], an input bit in MONID_Y [11:8], a direction bit
   in MONID_EN [19:16] (1 = output) and an enable in MONID_MASK [27:24]
   (0 = "gpio turned off") (RRG: GPIO_MONID, pp. 3-196-3-197 /
   PDF 214-215). On a single-CRTC chip the VGA output's DDC clock is pad
   2 and its data is pad 1 (xf86-video-r128 r128_output.c
   R128SetupConnectors). The lines are open drain: the driver keeps A at
   0 and drives a line low by setting its EN bit, releasing it to the
   board's pull-up by clearing EN (r128_output.c R128I2CPutBits). */
#define RAGE128_GPIO_MONID              0x0068
#define RAGE128_GPIO_MONID_A_DDC_CLK    (1 << 2)  /* MONID2_A */
#define RAGE128_GPIO_MONID_A_DDC_DAT    (1 << 1)  /* MONID1_A */
#define RAGE128_GPIO_MONID_Y_DDC_CLK    (1 << 10) /* MONID2_Y, clock level */
#define RAGE128_GPIO_MONID_Y_DDC_DAT    (1 << 9)  /* MONID1_Y, data level */
#define RAGE128_GPIO_MONID_EN_DDC_CLK   (1 << 18) /* MONID2_EN, 1 = drive clock */
#define RAGE128_GPIO_MONID_EN_DDC_DAT   (1 << 17) /* MONID1_EN, 1 = drive data */
#define RAGE128_GPIO_MONID_MASK_DDC_CLK (1 << 26) /* MONID2_MASK, 1 = gpio on */
#define RAGE128_GPIO_MONID_MASK_DDC_DAT (1 << 25) /* MONID1_MASK, 1 = gpio on */

/* AMCGPIO: 26 pads of the AMC (multimedia connector) used as
   general-purpose I/O. Each MASK bit makes the same bit of A and EN
   take effect, an EN bit of 1 makes the pad an output driving its A
   bit, and Y reads the level on the pin (RRG: AMCGPIO_MASK, p. 3-209 /
   PDF 227; RRG: AMCGPIO_A_REG and AMCGPIO_Y_REG, p. 3-211 / PDF 229;
   RRG: AMCGPIO_EN_REG, p. 3-212 / PDF 230). The guide also lists
   _MIR copies at 0x9c-0xa8, reachable through the I/O block, with the
   same fields (RRG: AMCGPIO_MASK_MIR, pp. 3-198-3-199 / PDF 216-217);
   the device treats them as the same registers. Bits above the 26 pads
   read 0. On a single card nothing is wired to the pads, so a pin the
   chip does not drive reads 0, the Y reset value. A board that does
   wire them supplies the pin levels through the amcgpio_in word and is
   told of every latch write through the amcgpio_notify hook. */
#define RAGE128_AMCGPIO_MASK_MIR 0x009c
#define RAGE128_AMCGPIO_A_MIR    0x00a0
#define RAGE128_AMCGPIO_Y_MIR    0x00a4
#define RAGE128_AMCGPIO_EN_MIR   0x00a8
#define RAGE128_AMCGPIO_MASK     0x0194
#define RAGE128_AMCGPIO_A        0x01a0
#define RAGE128_AMCGPIO_Y        0x01a4
#define RAGE128_AMCGPIO_EN       0x01a8
#define RAGE128_AMCGPIO_PINS     0x03ffffff

/* CRTC / DAC control group (RRG: CRTC_GEN_CNTL, pp. 3-61-3-63 /
   PDF 79-81; RRG: CRTC_EXT_CNTL, pp. 3-63-3-65 / PDF 81-83; RRG:
   DAC_CNTL, pp. 3-123-3-125 / PDF 141-143; RRG: CRTC_STATUS, p. 3-66 /
   PDF 84). */
#define RAGE128_CRTC_GEN_CNTL 0x0050
#define RAGE128_CRTC_EXT_CNTL 0x0054
#define RAGE128_DAC_CNTL      0x0058
#define RAGE128_CRTC_STATUS   0x005c

#define RAGE128_CRTC_GEN_CNTL_DEFAULT 0x04000000 /* CRTC_DISP_REQ_EN_B = 1 */
#define RAGE128_CRTC_DBL_SCAN_EN      (1 << 0)
#define RAGE128_CRTC_INTERLACE_EN     (1 << 1)
#define RAGE128_CRTC_PIX_WIDTH_SHIFT  8          /* [10:8] 1=4 2=8 3=15 4=16 5=24 6=32 bpp */
#define RAGE128_CRTC_CUR_EN           (1 << 16)
#define RAGE128_CRTC_EXT_DISP_EN      (1 << 24)  /* 0 = VGA, 1 = extended display */
#define RAGE128_CRTC_EN               (1 << 25)  /* 0 = CRTC held in reset */
#define RAGE128_CRTC_DISP_REQ_EN_B    (1 << 26)  /* active low: 1 = display requests disabled */

#define RAGE128_CRTC_EXT_CNTL_DEFAULT 0x00200000 /* DFIFO_EXTSENSE = 1 */
#define RAGE128_CRTC_XCRT_CNT_EN      (1 << 6)   /* VGA_XCRT_CNT_EN: extended CRTC address
                                                    counter */
#define RAGE128_CRTC_VGA_ATI_LINEAR   (1 << 3)
#define RAGE128_VGA_BLINK_RATE_SHIFT  1          /* [2:1] frames per VGA blink */
#define RAGE128_CRTC_HSYNC_DIS        (1 << 8)
#define RAGE128_CRTC_VSYNC_DIS        (1 << 9)
#define RAGE128_CRTC_DISPLAY_DIS      (1 << 10)
#define RAGE128_VGA_MEM_PS_EN         (1 << 19) /* use MEM_VGA_WP_SEL and MEM_VGA_RP_SEL in
                                                   VGA modes */

#define RAGE128_DAC_CNTL_DEFAULT 0xff00000a /* DAC_RANGE_CNTL = 2, DAC_CMP_EN = 1,
                                               DAC_MASK = 0xff */
#define RAGE128_DAC_CMP_OUTPUT   (1 << 7)   /* RO monitor-sense comparator */
#define RAGE128_DAC_8BIT_EN      (1 << 8)
#define RAGE128_DAC_4BPP_PIX_ORDER (1 << 9) /* 1 = low nibble is the left pixel */
#define RAGE128_DAC_VGA_ADR_EN   (1 << 13) /* 1 = palette reachable at the VGA
                                              I/O DAC addresses in extended
                                              modes */
#define RAGE128_DAC_PDWN         (1 << 15) /* analog DAC macro powered down */
#define RAGE128_DAC_CRC_EN       (1 << 19) /* CRC of the data sent to the DAC,
                                              starting at the next vertical
                                              blank and running one field or
                                              frame */

#define RAGE128_CRTC_STATUS_DEFAULT 0x80000000 /* FIX_VSYNC_TIMING = 1 */
/* CRTC_STATUS bit 0 CRTC_VBLANK_CUR reads 1 while the raster is in
   vertical blank; bit 1 CRTC_VBLANK_SAVE stays set from a vertical
   blank until software writes 1 to it (CRTC_VBLANK_SAVE_CLEAR). */

/* DAC palette access through the register aperture; the legacy ports
   3C6-3C9 stay with the VGA core (RRG: PALETTE_INDEX, pp. 3-199-3-200 /
   PDF 217-218). With DAC_8BIT_EN clear, writes shift the 6-bit value
   left by 2 and reads shift right by 2 (RRG: DAC_CNTL, p. 3-124 /
   PDF 142). */
#define RAGE128_PALETTE_INDEX 0x00b0 /* [7:0] write index, [23:16] read index */
#define RAGE128_PALETTE_DATA  0x00b4 /* [7:0] B, [15:8] G, [23:16] R */

/* Configuration block (RRG: CONFIG_CNTL to CONFIG_MEMSIZE,
   pp. 3-10-3-12 / PDF 28-30). */
#define RAGE128_CONFIG_CNTL    0x00e0 /* CFG_ATI_REV_ID [19:16] is read-only */
/* CFG_ATI_REV_ID, the silicon revision. The guide's default column
   shows 0, but ATI's Windows 98 miniVDD (ati2vxaa.vxd 4.13.7192) needs
   a real value: it looks up the (device id, revision) pair in an ASIC
   table, starting at the reported revision and counting down to 0, and
   fails hardware init (Device Manager Code 24) if no row matches, so a
   revision reported lower than the table's row fails.
     0x5046 (Rage 128 Pro, "PF"): the only row has revision 1.
     0x5245 (Rage 128 GL, "RE"):  rows for revisions 2 and 3. The XPERT
                                  128's revision has not been read from a
                                  card; 3 is the higher of the two.
   (RE: ati2vxaa.vxd @7fc4, the lookup, which scans the table at file
   offset 0x7040.) */
#define RAGE128_CFG_ATI_REV_PF 0x00010000
#define RAGE128_CFG_ATI_REV_RE 0x00030000
#define RAGE128_CFG_VGA_IO_DIS 0x00000200 /* [9] VGA I/O decode off; reset value from the
                                             strap */
#define RAGE128_CONFIG_XSTRAP  0x00e4 /* read-only strap fields
                                         (RRG: CONFIG_XSTRAP, p. 3-11 / PDF 29); the strap
                                         pins and bus modes are in
                                         GCS: External Straps, pp. 4-32-4-33 / PDF 80-81 */
#define RAGE128_XSTRAP_VGA_DISABLE 0x00000001 /* [0] pin SAD7: not the system's VGA */
#define RAGE128_XSTRAP_BUS_CLK_SEL 0x00000002 /* [1] pin HSYNC: 1 = reference clock,
                                                 0 = PLL clock */
#define RAGE128_XSTRAP_IDSEL       0x00000004 /* [2] pin SAD6: AGP IDSEL on AD17, not AD16 */
#define RAGE128_XSTRAP_BUSTYPE     0x00000030 /* [5:4] pins SAD[5:4]; with BUS_CLK_SEL selects
                                                 the bus mode (GCS Table 4-8) */
#define RAGE128_XSTRAP_ADDIN_CARD  0x00002000 /* [13] pin ROMCS#: add-in card, BIOS cycles
                                                 occur */
#define RAGE128_CONFIG_BONDS   0x00e8 /* read-only bond options */
#define RAGE128_GEN_RESET_CNTL 0x00f0 /* soft resets per block (RRG: GEN_RESET_CNTL,
                                         pp. 3-200-3-201 / PDF 218-219) */
#define RAGE128_GEN_SOFT_RESET_GUI 0x00000001 /* [0]; drivers pulse it to recover a hung engine
                                                 (linux r128 DRM r128_cce.c
                                                 r128_do_engine_reset) */
#define RAGE128_GEN_STATUS     0x00f4 /* read-only MPP status, 0 when idle */
#define RAGE128_CONFIG_MEMSIZE 0x00f8 /* [25:0] framebuffer size in bytes, bits 20:0
                                         hardwired 0 */
/* Aperture geometry, all read-only (RRG: CONFIG_APER_0_BASE and
   CONFIG_APER_1_BASE, p. 3-12 / PDF 30; RRG: CONFIG_APER_SIZE to
   CONFIG_MEMSIZE_EMBEDDED, pp. 3-202-3-203 / PDF 220-221). */
#define RAGE128_CONFIG_APER_0_BASE     0x0100 /* base of linear aperture 0 */
#define RAGE128_CONFIG_APER_1_BASE     0x0104 /* base of linear aperture 1; the field's
                                                 low bit (register bit 25) reads 1 */
#define RAGE128_CONFIG_APER_SIZE       0x0108 /* size of each linear aperture, bits 24:0
                                                 hardwired 0 */
#define RAGE128_CONFIG_REG_1_BASE      0x010c /* base of register aperture 1; the field's
                                                 low bit (register bit 13) reads 1 */
#define RAGE128_CONFIG_REG_APER_SIZE   0x0110 /* size of each register aperture, 0x2000 */
/* 32 MB, the guide's default. Bits 24:0 are hardwired to zero, so the
   value cannot follow the fitted VRAM: a 16 MB card still reads 32 MB. */
#define RAGE128_CONFIG_APER_SIZE_VAL   0x02000000
/* Base fields. For APER_1_BASE and REG_1_BASE the bit the guide calls
   hardwired to 1 is the low bit of the field, register bit 25 or 13,
   not register bit 0. */
#define RAGE128_APER_0_BASE_MASK       0xfc000000 /* [31:26], 64 MB granularity */
#define RAGE128_APER_1_BASE_MASK       0xfe000000 /* [31:25], 32 MB granularity */
#define RAGE128_REG_1_BASE_MASK        0xffffe000 /* [31:13], 8 KB granularity  */
/* One register aperture is 8 KB (CONFIG_REG_APER_SIZE); BAR2 holds two
   identical copies. */
#define RAGE128_REG_APER_MASK          0x00001fff
#define RAGE128_CONFIG_MEMSIZE_EMB     0x0114 /* embedded memory size, "reserved for
                                                 future use", reads 0 */
/* Bus master chunk registers (Bus Master supplement, page BM-2).
   BM_CHUNK_0_VAL holds BM_VIDCAP_CHUNK [20:0], reset value FFh, and
   three FORCE_TO_PCI bits, reset value 0, each "0 = transfer on AGP, if
   bus type is AGP, otherwise transfer on PCI bus; 1 = force transfer to
   be on PCI bus, regardless of bus type". The supplement does not say
   which transfers each bit covers. Going by the names,
   BM_PM4_RD_FORCE_TO_PCI covers the CCE's own bus reads and
   BM_GLOBAL_FORCE_TO_PCI all of them; BM_PTR_FORCE_TO_PCI belongs to
   the bus-master descriptor engine, which the device does not have. The
   bit masks are next to the GART code in vid_ati_rage128.h. */
#define RAGE128_BM_CHUNK_0_VAL         0x0a18
#define RAGE128_BM_CHUNK_0_VAL_DEFAULT 0x000000ff /* BM_VIDCAP_CHUNK = FFh, force bits 0 */
#define RAGE128_BM_CHUNK_1_VAL         0x0a1c
/* BM_VIP0_CHUNK to BM_VIP3_CHUNK, one byte each from [7:0] up to
   [31:24], each with reset value 0Fh (Bus Master supplement, page
   BM-2). */
#define RAGE128_BM_CHUNK_1_VAL_DEFAULT 0x0f0f0f0f
/* BM_QUEUE_FREE_STATUS (read-only) at its reset value: each queue's
   free-slot count at its default and no transfer active. The device has
   no bus master queues, so the register always reads this. BM_VIP0_FREE
   to BM_VIP3_FREE are 2 each, BM_VIDCAP_FREE is 8 and every _ACTIVE bit
   is 0 (RRG: BM_QUEUE_FREE_STATUS, pp. 3-223-3-224 / PDF 241-242). */
#define RAGE128_BM_QUEUE_FREE_STATUS 0x0a14
#define RAGE128_BM_QUEUE_FREE_IDLE   0x00802222
#define RAGE128_CONFIG_MEMSIZE_MASK 0x03e00000 /* writable bits: 2 MB granularity */

/* Test and debug (RRG: TEST_DEBUG_CNTL, p. 3-129 / PDF 147; RRG:
   TEST_DEBUG_MUX, p. 3-132 / PDF 150; RRG: HW_DEBUG, p. 3-133 /
   PDF 151). */
#define RAGE128_TEST_DEBUG_CNTL 0x0120
#define RAGE128_TEST_DEBUG_MUX  0x0124
#define RAGE128_HW_DEBUG        0x0128
#define RAGE128_TEST_DEBUG_CLK_SHIFT 8 /* TEST_DEBUG_MUX.TEST_DEBUG_CLK [12:8]: which
                                          internal clock is sent to the test output */

/* Host path (RRG: HOST_PATH_CNTL, p. 3-149 / PDF 167). */
#define RAGE128_HOST_PATH_CNTL         0x0130
#define RAGE128_HOST_PATH_CNTL_DEFAULT 0x0000207f /* the per-field reset values */

/* Memory controller group (RRG: MEM_CNTL to MEM_INIT_LAT_TIMER,
   pp. 3-110-3-122 / PDF 128-140; RRG: MEM_SDRAM_MODE_REG, p. 3-205 /
   PDF 223). */
#define RAGE128_MEM_CNTL           0x0140
#define RAGE128_EXT_MEM_CNTL       0x0144
#define RAGE128_MEM_ADDR_CONFIG    0x0148
#define RAGE128_MEM_INTF_CNTL      0x014c
#define RAGE128_MEM_STR_CNTL       0x0150
#define RAGE128_MEM_INIT_LAT_TIMER 0x0154
#define RAGE128_MEM_SDRAM_MODE_REG 0x0158
#define RAGE128_MEM_CNTL_DEFAULT      0x08000300 /* MEM_LATENCY = 3, MEM_REFRESH_DIS = 1 */
#define RAGE128_MEM_CNTL_RO_MASK      0x00700000 /* [22:20] MEM_CTLR_STATUS, MEM_SEQNCR_STATUS,
                                                    MEM_ARBITER_STATUS: read-only busy bits,
                                                    0 when idle */
#define RAGE128_EXT_MEM_CNTL_DEFAULT  0x0000d67f /* the per-field reset values */
#define RAGE128_MEM_SDRAM_MODE_DEFAULT 0x00300000 /* MEM_CAS_LATENCY = 3 */
#define RAGE128_MEM_INIT_LAT_DEFAULT  0x3fffffff /* five 6-bit latencies, each 0x3f */

/* Pads, pixel cache and video mux (RRG: PAD_CTLR_STRENGTH, p. 3-206 /
   PDF 224; RRG: PC_MISC_CTL, p. 3-254 / PDF 272; RRG: VIDEOMUX_CNTL,
   p. 3-208 / PDF 226). */
#define RAGE128_PAD_CTLR_STRENGTH 0x0168
#define RAGE128_PC_MISC_CTL       0x0188
#define RAGE128_VIDEOMUX_CNTL     0x0190
#define RAGE128_PAD_CTLR_STRENGTH_DEFAULT 0x00010000 /* PAD_MANUAL_OVERRIDE = 1 */
#define RAGE128_VIDEOMUX_CNTL_DEFAULT     0x000507f3 /* the per-field reset values */

/* CRTC timing and scanout registers (RRG: CRTC_H_TOTAL_DISP to
   CRTC_PITCH, pp. 3-66-3-74 / PDF 84-92). */
#define RAGE128_CRTC_H_TOTAL_DISP    0x0200 /* [8:0] total, [23:16] display end; in character
                                               clocks of 8 pixels */
#define RAGE128_CRTC_H_SYNC_STRT_WID 0x0204 /* [2:0] start delay in pixels, [11:3] start in
                                               character clocks, [21:16] width, [23] polarity */
#define RAGE128_CRTC_V_TOTAL_DISP    0x0208 /* [10:0] total, [26:16] display end; in lines */
#define RAGE128_CRTC_V_SYNC_STRT_WID 0x020c /* [10:0] start, [20:16] width, [23] polarity */
#define RAGE128_CRTC_VLINE_CRNT_VLINE 0x0210 /* [10:0] interrupt line, [26:16] read-only
                                                current line */
#define RAGE128_CRTC_CRNT_FRAME      0x0214 /* [20:0] read-only frame counter (RRG:
                                               CRTC_CRNT_FRAME, p. 3-214 / PDF 232) */

/* Raster and frame snapshot (RRG: SNAPSHOT_VH_COUNTS to
   SNAPSHOT_VIF_COUNT, pp. 3-215-3-216 / PDF 233-234). The guide swaps
   the descriptions of the two SNAPSHOT_VH_COUNTS fields: it describes
   SNAPSHOT_HCOUNT as the vertical count and SNAPSHOT_VCOUNT as the
   horizontal one. The widths match the names, 9 bits for the horizontal
   total in character clocks and 11 bits for the vertical total in
   lines, so the device follows the names: HCOUNT [8:0], VCOUNT [26:16].
   The video-in-field counts read 0, since they count fields from the
   VIP block, which the device does not have; with no fields counted no
   automatic snapshot fires, and MANUAL_SNAPSHOT_NOW ([25], "snapshot
   taken immediately") is the only trigger. The N_VIF_COUNT description
   points to "CRTC_INT_CNTL register[8:7]" for the snapshot interrupt;
   the guide has no such register, and the snapshot interrupt is bit 3
   of GEN_INT_CNTL and GEN_INT_STATUS. */
#define RAGE128_SNAPSHOT_VH_COUNTS   0x0240 /* read-only H/V counts at the snapshot */
#define RAGE128_SNAPSHOT_F_COUNT     0x0244 /* [20:0] read-only frame count at the snapshot */
#define RAGE128_N_VIF_COUNT          0x0248 /* [9:0] compare value, [31] GENLOCK_SOURCE_SEL */
#define RAGE128_SNAPSHOT_VIF_COUNT   0x024C /* [20:0] read-only VIF counts, [25:24] control */
#define RAGE128_CRTC_GUI_TRIG_VLINE  0x0218 /* [10:0] start and [26:16] end line, [31] read-only:
                                               raster is between them */
#define RAGE128_CRTC_DEBUG           0x021c
#define RAGE128_CRTC_OFFSET          0x0224 /* [24:0] byte offset (bits 2:0 hardwired 0),
                                               [30] read-only: new offset not yet shown,
                                               [31] lock */
#define RAGE128_CRTC_OFFSET_CNTL     0x0228 /* tiling and flip control; bits 31:30 are the
                                               same two bits as in CRTC_OFFSET */
#define RAGE128_CRTC_TILE_LINE_MASK  0x0000001f /* CRTC_TILE_LINE [4:0]: start line mod 32. Tiles
                                                   are 16 lines high; the guide asks for mod 32
                                                   so the checkerboarding comes out right */
#define RAGE128_CRTC_TILE_ALIGN_MASK 0x00000700 /* CRTC_TILE_ALIGN [10:8]: alignment of the
                                                   tiled surface, 0 = 64 bytes, 1 = 2 KB,
                                                   2 = 4 KB, 3 = 8 KB, 4 = 16 KB */
#define RAGE128_CRTC_TILE_EN         (1 << 15)  /* display surface uses tiled addressing */
#define RAGE128_CRTC_OFFSET_FLIP_CNTL (1 << 16) /* new CRTC_OFFSET taken at 0 = vertical blank,
                                                   1 = any horizontal blank (RRG:
                                                   CRTC_OFFSET_CNTL, p. 3-72 / PDF 90) */
#define RAGE128_CRTC_PITCH           0x022c /* [9:0] line pitch in units of 8 pixels */
/* Hardware cursor: 64x64 at 2 bpp (color 0, color 1, transparent,
   inverse), enabled by CRTC_GEN_CNTL.CRTC_CUR_EN (RRG: CRTC_GEN_CNTL,
   p. 3-62 / PDF 80; registers in RRG: CUR_OFFSET to CUR_CLR1,
   pp. 3-81-3-83 / PDF 99-101). */
#define RAGE128_CUR_OFFSET           0x0260 /* [24:0] image addr, 16-byte aligned; [31] lock */
#define RAGE128_CUR_HORZ_VERT_POSN   0x0264 /* vert [10:0], horz [26:16]; [31] lock */
#define RAGE128_CUR_HORZ_VERT_OFF    0x0268 /* vert [5:0], horz [21:16]; [31] lock */
#define RAGE128_CUR_CLR0             0x026c /* B [7:0], G [15:8], R [23:16] */
#define RAGE128_CUR_CLR1             0x0270 /* B [7:0], G [15:8], R [23:16] */
#define RAGE128_OVR_CLR              0x0230
#define RAGE128_OVR_WID_LEFT_RIGHT   0x0234
#define RAGE128_OVR_WID_TOP_BOTTOM   0x0238

/* DAC extras (RRG: DAC_EXT_CNTL, p. 3-217 / PDF 235; RRG: DAC_CRC_SIG,
   p. 3-126 / PDF 144). */
#define RAGE128_DAC_EXT_CNTL 0x0280
#define RAGE128_DAC_CRC_SIG  0x02cc

#define RAGE128_DAC_FORCE_BLANK_OFF_EN (1 << 4) /* DAC BLANK forced off */
#define RAGE128_DAC_FORCE_DATA_EN      (1 << 5) /* DAC inputs forced to DAC_FORCE_DATA */
#define RAGE128_DAC_FORCE_DATA_SEL_SHIFT 6      /* [7:6] forced channel: 0=R 1=G 2=B 3=all */
#define RAGE128_DAC_FORCE_DATA_SHIFT     8      /* [15:8] the 8-bit value */

/* Display FIFO DDA tuning (RRG: DDA_CONFIG to VGA_DDA_ON_OFF,
   pp. 3-76-3-77 / PDF 94-95). */
#define RAGE128_DDA_CONFIG     0x02e0
#define RAGE128_DDA_ON_OFF     0x02e4
#define RAGE128_VGA_DDA_CONFIG 0x02e8
#define RAGE128_VGA_DDA_ON_OFF 0x02ec

/* OV0, the hardware video overlay and its scaler. The RRG has a page
   only for OV0_COL_CONV and names a few other OV0 registers in its
   revision history; the register layouts are in the Multimedia
   supplement (OV0_REG_LOAD_CNTL, OV0_SCALE_CNTL, OV0_KEY_CNTL and the
   rest) and match the register macros of xf86-video-r128. The
   registers are double buffered. The X driver updates them as the
   supplement describes: set OV0_LOCK, poll until OV0_LOCK_READBACK
   reads 1, write the new set, clear OV0_LOCK (r128_video.c
   R128DisplayVideo422). The supplement says the new set takes effect at
   the next vertical blank after the unlock; the device applies it when
   the lock is released (latch model in vid_ati_rage128_ov0.c). */
#define RAGE128_OV0_BLOCK_BASE                 0x0400
#define RAGE128_OV0_BLOCK_END                  0x04ff
#define RAGE128_OV0_REG(off)                   (((off) - RAGE128_OV0_BLOCK_BASE) >> 2)

#define RAGE128_OV0_Y_X_START                  0x0400
#define RAGE128_OV0_Y_X_END                    0x0404
#define RAGE128_OV0_EXCLUSIVE_HORZ             0x0408
#define RAGE128_OV0_EXCLUSIVE_VERT             0x040c
#define RAGE128_OV0_REG_LOAD_CNTL              0x0410
#define RAGE128_OV0_REG_LD_CTL_LOCK                  0x00000001 /* OV0_LOCK: hold the double
                                                                   buffered registers */
#define RAGE128_OV0_REG_LD_CTL_VBLANK_DURING_LOCK    0x00000002 /* read-only: a vertical blank
                                                                   came while the lock was held */
#define RAGE128_OV0_REG_LD_CTL_STALL_GUI_UNTIL_FLIP  0x00000004
#define RAGE128_OV0_REG_LD_CTL_LOCK_READBACK         0x00000008 /* read-only: the lock took
                                                                   effect */
#define RAGE128_OV0_SCALE_CNTL                 0x0420
#define RAGE128_OV0_SCALER_PIX_EXPAND                0x00000001
#define RAGE128_OV0_SCALER_Y2R_TEMP                  0x00000002
#define RAGE128_OV0_SCALER_SIGNED_UV                 0x00000010
#define RAGE128_OV0_SCALER_SURFAC_FORMAT             0x00000f00
#define RAGE128_OV0_SCALER_FORMAT_SHIFT              8
#define RAGE128_OV0_SCALER_SOURCE_15BPP              0x3
#define RAGE128_OV0_SCALER_SOURCE_16BPP              0x4
#define RAGE128_OV0_SCALER_SOURCE_32BPP              0x6
#define RAGE128_OV0_SCALER_SOURCE_YUV9               0x9
#define RAGE128_OV0_SCALER_SOURCE_YUV12              0xa /* planar 4:2:0 */
#define RAGE128_OV0_SCALER_SOURCE_VYUY422            0xb /* packed, YUY2 fourcc */
#define RAGE128_OV0_SCALER_SOURCE_YVYU422            0xc /* packed, UYVY fourcc */
#define RAGE128_OV0_SCALER_SMART_SWITCH              0x00008000
#define RAGE128_OV0_SCALER_DOUBLE_BUFFER             0x01000000
#define RAGE128_OV0_SCALER_ENABLE                    0x40000000
#define RAGE128_OV0_SCALER_SOFT_RESET                0x80000000
#define RAGE128_OV0_V_INC                      0x0424
#define RAGE128_OV0_P1_V_ACCUM_INIT            0x0428
#define RAGE128_OV0_P23_V_ACCUM_INIT           0x042c
#define RAGE128_OV0_P1_BLANK_LINES_AT_TOP      0x0430
#define RAGE128_OV0_P23_BLANK_LINES_AT_TOP     0x0434
#define RAGE128_OV0_VID_BUF0_BASE_ADRS         0x0440
#define RAGE128_OV0_VID_BUF1_BASE_ADRS         0x0444
#define RAGE128_OV0_VID_BUF2_BASE_ADRS         0x0448
#define RAGE128_OV0_VID_BUF3_BASE_ADRS         0x044c
#define RAGE128_OV0_VID_BUF4_BASE_ADRS         0x0450
#define RAGE128_OV0_VID_BUF5_BASE_ADRS         0x0454
#define RAGE128_OV0_VID_BUF_PITCH0_VALUE       0x0460
#define RAGE128_OV0_VID_BUF_PITCH1_VALUE       0x0464
#define RAGE128_OV0_AUTO_FLIP_CNTL             0x0470
#define RAGE128_OV0_SOFT_BUF_NUM                     0x00000007 /* base address register 0-5 */
#define RAGE128_OV0_SOFT_BUF_ODD                     0x00000010
#define RAGE128_OV0_SOFT_EOF_TOGGLE                  0x00000040 /* double buffered: a change
                                                                   submits the field above */
#define RAGE128_OV0_DEINTERLACE_PATTERN        0x0474
#define RAGE128_OV0_H_INC                      0x0480
#define RAGE128_OV0_STEP_BY                    0x0484
#define RAGE128_OV0_P1_H_ACCUM_INIT            0x0488
#define RAGE128_OV0_P23_H_ACCUM_INIT           0x048c
#define RAGE128_OV0_P1_X_START_END             0x0494
#define RAGE128_OV0_P2_X_START_END             0x0498
#define RAGE128_OV0_P3_X_START_END             0x049c
#define RAGE128_OV0_FILTER_CNTL                0x04a0
#define RAGE128_OV0_FOUR_TAP_COEF_0            0x04b0
#define RAGE128_OV0_FOUR_TAP_COEF_1            0x04b4
#define RAGE128_OV0_FOUR_TAP_COEF_2            0x04b8
#define RAGE128_OV0_FOUR_TAP_COEF_3            0x04bc
#define RAGE128_OV0_FOUR_TAP_COEF_4            0x04c0
#define RAGE128_OV0_COLOUR_CNTL                0x04e0
#define RAGE128_OV0_VIDEO_KEY_CLR              0x04e4
#define RAGE128_OV0_VIDEO_KEY_MSK              0x04e8
#define RAGE128_OV0_GRAPHICS_KEY_CLR           0x04ec
#define RAGE128_OV0_GRAPHICS_KEY_MSK           0x04f0
#define RAGE128_OV0_KEY_CNTL                   0x04f4
/* OV0_KEY_CNTL key functions. The _EQ / _NE names follow the X driver's
   macros R128_VIDEO_KEY_FN_EQ (4) and R128_VIDEO_KEY_FN_NE (5) in
   xf86-video-r128. The Multimedia supplement (OV0_KEY_CNTL) defines the
   two codes the other way round, 4 = the key color differs and 5 = it
   matches, and vid_ati_rage128_ov0.c implements the supplement's
   meaning. */
#define RAGE128_OV0_VIDEO_KEY_FN_MASK                0x00000007
#define RAGE128_OV0_VIDEO_KEY_FN_FALSE               0x0
#define RAGE128_OV0_VIDEO_KEY_FN_TRUE                0x1
#define RAGE128_OV0_VIDEO_KEY_FN_EQ                  0x4
#define RAGE128_OV0_VIDEO_KEY_FN_NE                  0x5
#define RAGE128_OV0_GRAPHIC_KEY_FN_MASK              0x00000070
#define RAGE128_OV0_GRAPHIC_KEY_FN_SHIFT             4
#define RAGE128_OV0_CMP_MIX_AND                      0x00000100
#define RAGE128_OV0_TEST                       0x04f8
#define RAGE128_OV0_COL_CONV                   0x04fc /* RRG: OV0_COL_CONV, p. 3-217 / PDF 235 */

/* DVD subpicture block. The RRG lists "Subpicture registers" as a
   group (RRG: Multimedia Registers, p. 2-3 / PDF 13) but has no page for
   any of them. SUBPIC_CNTL is ATI's name: the SDK clears it during a
   mode set to "disable subpicture decoding" (SDK: Manually Setting a
   Display Mode, p. 3-12 / PDF 58), without giving its offset or fields.
   The other names are this file's own. The offsets and fields come from
   ATI's motion-compensation driver, whose DVD subpicture command parser
   programs the block through a register write helper and a read helper
   (RE: ATIMCRAA.DLL @b01128d0, the parser; @b0117140 and @b01174d0, the
   helpers). The parser updates SUBPIC_CNTL by read-modify-write, so the
   device keeps what is written and reads it back; only the new-frame
   strobe bit reads 0. */
#define RAGE128_SUBPIC_BLOCK_BASE              0x0540
#define RAGE128_SUBPIC_BLOCK_END               0x0588
#define RAGE128_SUBPIC_REG(off)                (((off) -RAGE128_SUBPIC_BLOCK_BASE) >> 2)
#define RAGE128_SUBPIC_CNTL                    0x0540
#define RAGE128_SUBPIC_CNTL_DISPLAY_EN               0x00000001 /* set by the DVD start-display
                                                                   command, cleared by
                                                                   stop-display */
#define RAGE128_SUBPIC_CNTL_NEW_FRAME                0x00000002 /* strobe, reads back 0 */
#define RAGE128_SUBPIC_CNTL_SHOW                     0x00000004 /* pulsed by the driver's show and
                                                                   hide handler; the display itself
                                                                   follows bit 0 */
#define RAGE128_SUBPIC_CNTL_FORCED_MASK              0x00000030 /* forced-display pair, set and
                                                                   cleared at the end of a command
                                                                   record */
#define RAGE128_SUBPIC_CNTL_FIELD_MODE               0x00000100
#define RAGE128_SUBPIC_COLOR_CONTRAST          0x0544 /* one 4-bit palette index per pixel
                                                         class in [31:16] (class c at bit
                                                         4c + 16), one 4-bit contrast per
                                                         class in [15:0] */
#define RAGE128_SUBPIC_DAREA_START             0x054c /* display area, [25:16] y, [9:0] x */
#define RAGE128_SUBPIC_DAREA_END               0x0550
#define RAGE128_SUBPIC_V_STEP                  0x0554 /* source lines per output line, modeled
                                                         as 16.16 fixed point */
#define RAGE128_SUBPIC_H_STEP                  0x0558 /* source pixels per output pixel, modeled
                                                         as 16.16 fixed point */
#define RAGE128_SUBPIC_PXD_A                   0x055c /* 2 bpp pixel data, field A */
#define RAGE128_SUBPIC_PXD_B                   0x0560 /* field B, one line after field A */
#define RAGE128_SUBPIC_LNCTL_A                 0x0564 /* line control data for the DVD
                                                         change-color-contrast command */
#define RAGE128_SUBPIC_LNCTL_B                 0x0568
#define RAGE128_SUBPIC_PITCH_LENGTH            0x056c /* [15:0] pixel data line pitch */
#define RAGE128_SUBPIC_HL_COLOR_CONTRAST       0x0570 /* button highlight, same packing as
                                                         0x0544 */
#define RAGE128_SUBPIC_HL_TOP                  0x0574 /* [25:16] y, [9:0] x */
#define RAGE128_SUBPIC_HL_BOTTOM               0x0578
#define RAGE128_SUBPIC_PALETTE_INDEX           0x057c /* 16 entries */
#define RAGE128_SUBPIC_PALETTE_DATA            0x0580 /* [23:16] Y [15:8] Cb [7:0] Cr */
#define RAGE128_SUBPIC_H_ACC                   0x0584 /* source start positions, modeled as
                                                         16.16 fixed point */
#define RAGE128_SUBPIC_V_ACC                   0x0588

/* VCLK_ECP_CNTL.ECP_DIV [9:8]: divides the pixel clock down to the
   overlay scaler clock, ECP, which may not exceed 125 MHz (RRG:
   VCLK_ECP_CNTL, p. 3-95 / PDF 113). The X driver folds the divider
   into OV0_H_INC: it computes h_inc = (src_w << (12 + ecp_div)) /
   drw_w and halves it, raising OV0_STEP_BY, while it is 2.0 or more
   (xf86-video-r128 r128_video.c R128ECP and R128DisplayVideo422). The
   source step per output pixel is therefore (H_INC << (STEP_BY - 1)) >>
   ECP_DIV. */
#define RAGE128_ECP_DIV_SHIFT 8

/* Surface translation and AGP control (RRG: SURFACE_DELAY to
   SURFACE3_INFO, pp. 3-224-3-231 / PDF 242-249; RRG: AGP_CNTL_B,
   pp. 3-231-3-234 / PDF 249-252). AGP_CNTL_B holds miscellaneous AGP
   override and fix-disable bits. */
#define RAGE128_SURFACE_DELAY 0x0b00
#define RAGE128_SURFACE0_LOWER_BOUND 0x0b04
#define RAGE128_SURFACE0_UPPER_BOUND 0x0b08
#define RAGE128_SURFACE0_INFO        0x0b0c
#define RAGE128_SURFACE1_LOWER_BOUND 0x0b14
#define RAGE128_SURFACE1_UPPER_BOUND 0x0b18
#define RAGE128_SURFACE1_INFO        0x0b1c
#define RAGE128_SURFACE2_LOWER_BOUND 0x0b24
#define RAGE128_SURFACE2_UPPER_BOUND 0x0b28
#define RAGE128_SURFACE2_INFO        0x0b2c
#define RAGE128_SURFACE3_LOWER_BOUND 0x0b34
#define RAGE128_SURFACE3_UPPER_BOUND 0x0b38
#define RAGE128_SURFACE3_INFO        0x0b3c
/* The bound fields (SURF0_LOWER, SURF0_UPPER and the like) are [25:0]
   with bits 5:0 hardwired to 0; the INFO registers hold a pitch select
   in [4:0]. */
#define RAGE128_SURF_BOUND_MASK      0x03ffffc0
#define RAGE128_SURF_INFO_MASK       0x0000001f
#define RAGE128_AGP_CNTL_B    0x0b44
#define RAGE128_SURFACE_DELAY_DEFAULT 0x00000131 /* the per-field reset values */

/* GUI engine status and configuration (RRG: GUI_DEBUG0, p. 3-245 /
   PDF 263; RRG: GUI_STAT, p. 3-244 / PDF 262; RRG: PC_GUI_MODE,
   p. 3-251 / PDF 269). */
#define RAGE128_GUI_DEBUG0  0x16a0
#define RAGE128_GUI_STAT    0x1740 /* read-only; idle = 0x00000040, GUI_FIFOCNT showing
                                      all 64 command FIFO entries free */
#define RAGE128_GUI_STAT_BUSY 0x80010040 /* GUI_ACTIVE [31] and PM4_BUSY [16] set.
                                            GUI_FIFOCNT still reads 64 free entries,
                                            because the device queues commands on the
                                            host side, far deeper than the chip's FIFO. */
/* FLUSH_1 to FLUSH_7: a write to FLUSH_n blocks the FIFO'd writes behind
   it until the level n engines are idle (RRG: FLUSH_1, pp. 3-234-3-235 /
   PDF 252-253). */
#define RAGE128_FLUSH_1     0x1704
#define RAGE128_FLUSH_2     0x1708
#define RAGE128_FLUSH_3     0x170c
#define RAGE128_FLUSH_4     0x1710
#define RAGE128_FLUSH_5     0x1714
#define RAGE128_FLUSH_6     0x1718
#define RAGE128_FLUSH_7     0x171c
#define RAGE128_WAIT_UNTIL  0x1720 /* [0] EVENT_CRTC_OFFSET: stall the command FIFO
                                      until the last CRTC_OFFSET written is on screen
                                      (RRG: WAIT_UNTIL, p. 3-239 / PDF 257) */
#define RAGE128_PC_GUI_MODE 0x1744
#define RAGE128_GUI_STAT_IDLE 0x00000040
#define RAGE128_GUI_STAT_ACTIVE 0x80000040 /* GUI_ACTIVE [31] only: the ring is drained
                                              but the 3D engine is still drawing */

/* Read-only copy of PCI configuration space (RRG: Description of
   Mapped Memory Apertures, Table 2-1, p. 2-5 / PDF 15). */
#define RAGE128_CONFIG_MIRROR_BASE 0x0f00
#define RAGE128_CONFIG_MIRROR_END  0x0fff

/* ------------------------------------------------------------------ */
/* PLL register file, reached through CLOCK_CNTL_INDEX/DATA (RRG: PLL
   Registers, pp. 3-87-3-105 / PDF 105-123). The guide lists exactly 18
   PLL registers, at indices 0x01-0x10, 0x12 and 0x13 (RRG: PLL Registers
   Sorted by Name, Table A-11, p. A-33 / PDF 313). Each define's comment
   gives its composed reset value where the guide has one. */
/* ------------------------------------------------------------------ */
#define RAGE128_PLL_REGS 0x20 /* PLL_ADDR is 5 bits; 0x00, 0x11 and 0x14 up are not listed */

#define RAGE128_PLL_CLK_PIN_CNTL      0x01 /* reset 0x000000f7; bit 3 is reserved */
#define RAGE128_PLL_PPLL_CNTL         0x02 /* reset 0x0000cc03 */
#define RAGE128_PLL_PPLL_REF_DIV      0x03
#define RAGE128_PLL_PPLL_DIV_0        0x04
#define RAGE128_PLL_PPLL_DIV_1        0x05
#define RAGE128_PLL_PPLL_DIV_2        0x06
#define RAGE128_PLL_PPLL_DIV_3        0x07
#define RAGE128_PLL_VCLK_ECP_CNTL     0x08
#define RAGE128_PLL_HTOTAL_CNTL       0x09
#define RAGE128_PLL_X_MPLL_REF_FB_DIV 0x0a
#define RAGE128_PLL_XPLL_CNTL         0x0b /* reset 0x0000cc03 */
#define RAGE128_PLL_XDLL_CNTL         0x0c /* reset 0x000b000b */
#define RAGE128_PLL_XCLK_CNTL         0x0d
#define RAGE128_PLL_MPLL_CNTL         0x0e /* reset 0x0000cc03 */
#define RAGE128_PLL_MCLK_CNTL         0x0f
#define RAGE128_PLL_AGP_PLL_CNTL      0x10 /* reset 0x7a770000 */
#define RAGE128_PLL_FCP_CNTL          0x12 /* reset 0x00000404 */
#define RAGE128_PLL_TEST_CNTL         0x13 /* reset 0x00000200 (PLL_MASK_READ_B = 1) */

/* CLK_PIN_CNTL reset image: the guide's per-field defaults compose to 0xf7,
   bit 3 being reserved (RRG: CLK_PIN_CNTL, p. 3-87 / PDF 105). */
#define RAGE128_PLL_CLK_PIN_CNTL_DEFAULT 0x000000f7u

/* PPLL_CNTL fields (RRG: PPLL_CNTL, pp. 3-88-3-89 / PDF 106-107). */
#define RAGE128_PPLL_RESET             (1 << 0)
#define RAGE128_PPLL_SLEEP             (1 << 1)
#define RAGE128_PPLL_ATOMIC_UPDATE_EN  (1 << 16)
#define RAGE128_PPLL_VGA_ATOMIC_UPDATE_EN (1 << 17)
#define RAGE128_PPLL_ATOMIC_UPDATE_SYNC   (1 << 18)

/* Bit 15 of PPLL_REF_DIV and of each PPLL_DIV_n is two fields at one
   position. Writing 1 (PPLL_ATOMIC_UPDATE_W) loads the new divider
   settings; reading it (PPLL_ATOMIC_UPDATE_R) returns 1 while that
   update is pending and 0 once it is done (RRG: PPLL_REF_DIV, p. 3-89 /
   PDF 107; the same pair is in each PPLL_DIV_n page). */
#define RAGE128_PPLL_ATOMIC_UPDATE (1 << 15)
#define RAGE128_PPLL_REF_DIV_MASK  0x3ff   /* [9:0], must be 2 or more */
/* PPLL_REF_DIV_SRC [17:16], the reference divider's input: 0 = XTALIN,
   1 = MPllClk/2, 2 = XPllClk/2; 3 is not listed. */
#define RAGE128_PPLL_REF_DIV_SRC_SHIFT 16
#define RAGE128_PPLL_REF_DIV_SRC_MASK  0x3
#define RAGE128_PPLL_REF_SRC_XTALIN    0
#define RAGE128_PPLL_REF_SRC_MPLL      1
#define RAGE128_PPLL_REF_SRC_XPLL      2
#define RAGE128_PPLL_FB_DIV_MASK   0x7ff   /* [10:0], must be 4 or more (RRG: PPLL_DIV_0,
                                              p. 3-90 / PDF 108) */
#define RAGE128_PPLL_POST_DIV_SHIFT 16     /* [18:16] */

/* VCLK_ECP_CNTL.VCLK_SRC_SEL [1:0]: 3 = the PPLL output drives the pixel
   clock. It resets to 0, the PCICLK pin, until a mode set selects the
   PPLL (RRG: VCLK_ECP_CNTL, p. 3-95 / PDF 113). */
#define RAGE128_VCLK_SRC_SEL_MASK 0x3
#define RAGE128_VCLK_SRC_PPLL     3

/* ------------------------------------------------------------------ */
/* 2D GUI engine (vid_ati_rage128_2d.c).                               */
/* ------------------------------------------------------------------ */
#define RAGE128_SRC_PITCH_OFFSET   0x1428 /* same packing as DST_PITCH_OFFSET (RRG:
                                             SRC_PITCH_OFFSET, p. 3-146 / PDF 164) */
#define RAGE128_DST_PITCH_OFFSET   0x142c /* offset [20:0] in 32-byte units, pitch [30:21]
                                             in units of 8 pixels, tile [31] (RRG:
                                             DST_PITCH_OFFSET, p. 3-137 / PDF 155) */
#define RAGE128_DP_GUI_MASTER_CNTL 0x146c /* fields below (RRG: DP_GUI_MASTER_CNTL,
                                             pp. 3-171-3-175 / PDF 189-193) */
#define RAGE128_BRUSH_Y_X          0x1474 /* brush alignment: BRUSH_X [4:0], BRUSH_Y [12:8],
                                             BRUSH_X_START [20:16] for lines (VR/GL RRM:
                                             BRUSH_Y_X, p. 7-31 / PDF 213) */
#define RAGE128_DP_BRUSH_BKGD_CLR  0x1478
#define RAGE128_DP_BRUSH_FRGD_CLR  0x147c
#define RAGE128_BRUSH_DATA0        0x1480 /* 8x8 mono pattern rows 0-3, a byte per row */
#define RAGE128_BRUSH_DATA1        0x1484 /* rows 4-7. The X driver writes the two halves
                                             of its 8x8 pattern here (xf86-video-r128
                                             r128_accel.c R128SetupForMono8x8PatternFill,
                                             in the XAA code of the driver's first
                                             revision) */
#define RAGE128_DP_SRC_FRGD_CLR    0x15d8
#define RAGE128_DP_SRC_BKGD_CLR    0x15dc
/* Color compare (RRG: CLR_CMP_CLR_SRC to CLR_CMP_MSK, pp. 3-178-3-179 /
   PDF 196-197). */
#define RAGE128_CLR_CMP_CNTL       0x15c0
#define RAGE128_CLR_CMP_CLR_SRC    0x15c4
#define RAGE128_CLR_CMP_CLR_DST    0x15c8
#define RAGE128_CLR_CMP_MASK       0x15cc /* CLR_CMP_MSK */
#define RAGE128_GUI_SCRATCH_REG0   0x15e0 /* GUI_SCRATCH_REG0 to REG5 run up to 0x15f4
                                             (RRG: GUI_SCRATCH_REG0, p. 3-240 / PDF 258) */
/* Auxiliary scissors: up to three more destination clip rectangles on top
   of the main scissor. AUXn_SC_MODE 0 = additive (combined with the
   other scissors by OR), 1 = subtractive (AND NOT); the bounds are 14-bit
   signed, range -8192 to 8191 (RRG: AUX_SC_CNTL, pp. 3-156-3-157 /
   PDF 174-175; RRG: AUX1_SC_LEFT, p. 3-157 / PDF 175). The Linux DRM
   loads its clip rectangles here with right and bottom set to x2 - 1
   and y2 - 1, so the bounds are inclusive (linux r128 DRM r128_state.c
   r128_emit_clip_rects). */
#define RAGE128_AUX_SC_CNTL        0x1660
#define RAGE128_AUX_SC_ENB_MASK    0x15u /* AUX1_SC_ENB, AUX2_SC_ENB, AUX3_SC_ENB: bits 0,
                                            2 and 4 */
#define RAGE128_AUX1_SC_LEFT       0x1664 /* AUX1 right, top, bottom at 0x1668-0x1670 */
#define RAGE128_AUX3_SC_BOTTOM     0x1690 /* AUX2 block 0x1674-0x1680, AUX3 0x1684-0x1690 */
/* DP_CNTL (RRG: DP_CNTL, pp. 3-165-3-166 / PDF 183-184). */
#define RAGE128_DP_CNTL            0x16c0
#define RAGE128_DP_CNTL_DST_X_DIR  (1 << 0) /* 1 = left to right */
#define RAGE128_DP_CNTL_DST_Y_DIR  (1 << 1) /* 1 = top to bottom */
#define RAGE128_DP_CNTL_DST_Y_MAJOR (1 << 2) /* 1 = Y-major Bresenham line */
#define RAGE128_DP_CNTL_DST_X_TILE (1 << 3) /* rectangular tiling in X; the device moves
                                               DST_X on by the drawn width after each
                                               draw */
#define RAGE128_DP_CNTL_DST_Y_TILE (1 << 4) /* rectangular tiling in Y; the device moves
                                               DST_Y on by the drawn height */
#define RAGE128_DP_CNTL_DST_LAST_PEL (1 << 5) /* 1 = draw the last pixel of a line */
#define RAGE128_DP_CNTL_POLY_LINE  (1 << 15) /* 1 = not the last line of a polyline */
#define RAGE128_DP_DATATYPE        0x16c4
#define RAGE128_DP_MIX             0x16c8
#define RAGE128_DP_WRITE_MASK      0x16cc
#define RAGE128_DEFAULT_OFFSET     0x16e0 /* RRG: DEFAULT_OFFSET, p. 3-243 / PDF 261 */
#define RAGE128_DEFAULT_PITCH      0x16e4
#define RAGE128_DEFAULT_SC_BOTTOM_RIGHT 0x16e8
#define RAGE128_SC_TOP_LEFT        0x16ec /* RRG: SC_TOP_LEFT, p. 3-161 / PDF 179 */
#define RAGE128_SC_BOTTOM_RIGHT    0x16f0
#define RAGE128_PC_GUI_CTLSTAT     0x1748 /* RRG: PC_GUI_CTLSTAT, p. 3-253 / PDF 271 */

/* Draws started by a register write (the direct 2D path of the X and
   Linux drivers). The packed coordinates hold y in [29:16] and x in
   [13:0], signed. Writing a size register starts the draw with the
   state already loaded: DST_WIDTH_HEIGHT = (w << 16) | h for a fill,
   DST_HEIGHT_WIDTH = (h << 16) | w for a copy (xf86-video-r128 r128_exa.c
   R128Solid and R128Copy). */
/* The single-field forms of the packed 2D state (RRG: DST_OFFSET to
   DST_WIDTH_X_INCY, pp. 3-137-3-142 / PDF 155-160; RRG: SRC_OFFSET to
   SRC_SC_BOTTOM_RIGHT, pp. 3-146-3-148 / PDF 164-166; RRG: SC_LEFT,
   p. 3-155 / PDF 173). Coordinates and sizes are [13:0]; offsets are byte
   addresses in [25:0] with bits 3:0 hardwired to zero; pitches are in
   units of 8 pixels in [9:0], with the tile bit at [16] (DST_PITCH also
   has DST_PITCH_ADJ in [18:17]). */
#define RAGE128_DST_OFFSET         0x1404
#define RAGE128_DST_PITCH          0x1408
#define RAGE128_DST_WIDTH          0x140c
#define RAGE128_DST_HEIGHT         0x1410
#define RAGE128_SRC_X              0x1414
#define RAGE128_SRC_Y              0x1418
#define RAGE128_DST_X              0x141c
#define RAGE128_DST_Y              0x1420
#define RAGE128_SRC_Y_X            0x1434
#define RAGE128_DST_Y_X            0x1438
#define RAGE128_DST_WIDTH_X        0x1588 /* write-only x and width; the device starts the
                                             draw on the write */
#define RAGE128_DST_WIDTH_X_INCY   0x159c /* as DST_WIDTH_X, then y += height */
#define RAGE128_DST_HEIGHT_Y       0x15a0 /* write-only y and height; starts the draw */
#define RAGE128_SRC_OFFSET         0x15ac
#define RAGE128_SRC_PITCH          0x15b0
#define RAGE128_DST_WIDTH_BW       0x15b4 /* write-only width for a block-write fill; the
                                             guide calls it "an initiator register" (RRG:
                                             DST_WIDTH_BW, p. 3-236 / PDF 254) */
#define RAGE128_SC_LEFT            0x1640
#define RAGE128_SC_RIGHT           0x1644
#define RAGE128_SC_TOP             0x1648
#define RAGE128_SC_BOTTOM          0x164c
#define RAGE128_SRC_SC_RIGHT       0x1654
#define RAGE128_SRC_SC_BOTTOM      0x165c
#define RAGE128_SRC_SC_BOTTOM_RIGHT 0x16f4 /* write-only, right [13:0], bottom [29:16] */
/* The _X_Y forms hold the same coordinates in the other order: x in
   [29:16] and y in [13:0], where the _Y_X forms above have x in [13:0]
   and y in [29:16] (RRG: DST_X_Y, pp. 3-138-3-139 / PDF 156-157; RRG:
   SRC_X_Y, p. 3-147 / PDF 165). The Windows 2000 display driver's
   gradient fill, which draws the window captions, writes the
   destination through DST_X_Y and starts the draw with DST_HEIGHT_WIDTH
   (RE: ati2dvaa.dll @00022184). */
#define RAGE128_SRC_X_Y            0x1590
#define RAGE128_DST_X_Y            0x1594
#define RAGE128_DST_HEIGHT_WIDTH   0x143c
#define RAGE128_DST_WIDTH_HEIGHT   0x1598

/* Bresenham lines (RRG: DST_BRES_LNTH to DST_BRES_DEC, p. 3-142 /
   PDF 160; RRG: DP_CNTL_XDIR_YDIR_YMAJOR, p. 3-170 / PDF 188). The X
   driver writes the direction register, the start point, ERR, INC =
   minor and DEC = -major, and last LNTH, which starts the line
   (xf86-video-r128 r128_accel.c R128SubsequentSolidBresenhamLine, in the
   XAA code of the driver's first revision). The device's walker adds INC
   to the error term at every pixel and DEC as well at each minor-axis
   step. */
#define RAGE128_DST_BRES_ERR       0x1628 /* [19:0] signed error term */
#define RAGE128_DST_BRES_INC       0x162c /* [19:0] signed */
#define RAGE128_DST_BRES_DEC       0x1630 /* [19:0] signed */
#define RAGE128_DST_BRES_LNTH      0x1634 /* [13:0] length in pixels; the write starts the
                                             line */
#define RAGE128_DP_CNTL_XDIR_YDIR_YMAJOR 0x16d0 /* line direction and major axis */
#define RAGE128_DP_LINE_Y_MAJOR    (1u << 2)  /* 1 = Y major axis, 0 = X major */
#define RAGE128_DP_LINE_Y_DIR      (1u << 15) /* 1 = top-to-bottom, 0 = bottom-to-top */
#define RAGE128_DP_LINE_X_DIR      (1u << 31) /* 1 = left-to-right, 0 = right-to-left */

/* Host data: the host streams the source dwords of a draw through
   HOST_DATA0-7 (RRG: HOST_DATA0, pp. 3-151-3-153 / PDF 169-171) and
   writes the last dword of the operation to HOST_DATA_LAST, which tells
   the engine the host data is complete (SDK: Monochrome Expansion,
   p. 4-17 / PDF 91). The X driver's color-expansion path does the same,
   ending only the final scanline on HOST_DATA_LAST (xf86-video-r128
   r128_accel.c R128SubsequentColorExpandScanline, in the XAA code of
   the driver's first revision). */
#define RAGE128_HOST_DATA0         0x17c0
#define RAGE128_HOST_DATA7         0x17dc
#define RAGE128_HOST_DATA_LAST     0x17e0

/* DP_GUI_MASTER_CNTL fields (RRG: DP_GUI_MASTER_CNTL, pp. 3-171-3-175 /
   PDF 189-193). */
#define RAGE128_GMC_SRC_PITCH_OFFSET_LEAVE (1 << 0) /* 0 = load from DEFAULT_OFFSET and
                                                       DEFAULT_PITCH */
#define RAGE128_GMC_DST_PITCH_OFFSET_LEAVE (1 << 1)
#define RAGE128_GMC_SRC_CLIP_LEAVE         (1 << 2)
#define RAGE128_GMC_DST_CLIP_LEAVE         (1 << 3)
#define RAGE128_GMC_BRUSH_TYPE(g)   (((g) >> 4) & 0xf)  /* 13 = solid, foreground color */
#define RAGE128_GMC_DST_DATATYPE(g) (((g) >> 8) & 0xf)
#define RAGE128_GMC_SRC_DATATYPE(g) (((g) >> 12) & 0x3) /* 0 and 1 mono, 3 color */
#define RAGE128_GMC_ROP3(g)         (((g) >> 16) & 0xff)
#define RAGE128_GMC_SRC_SOURCE(g)   (((g) >> 24) & 0x7) /* 2 memory, 3 and 4 host data */
/* GMC_3D_FCN_EN: 0 clears SCALE_3D_FCN (one of the guide's two
   descriptions adds Z_EN and STENCIL_EN), 1 leaves it alone (RRG:
   DP_GUI_MASTER_CNTL, p. 3-174 / PDF 192). While SCALE_3D_FCN is
   nonzero, DP_SRC_SOURCE is ignored and the source data comes from the
   3D/scaler pipeline (RRG: DP_MIX, p. 3-171 / PDF 189). The Windows 2000
   gradient fill sets this bit and draws through the scaler (RE:
   ati2dvaa.dll @00021f28). */
#define RAGE128_GMC_3D_FCN_EN        (1u << 27)
#define RAGE128_GMC_CLR_CMP_CNTL_DIS (1 << 28)
#define RAGE128_GMC_AUX_CLIP_DIS     (1 << 29)
#define RAGE128_GMC_WR_MSK_DIS       (1 << 30)
/* GMC_LD_BRUSH_Y_X, for CCE packets only: 1 = the packet carries a
   BRUSH_Y_X dword (VR/GL RRM: DP_GUI_MASTER_CNTL, p. 7-41 / PDF 223);
   the Pro guide words it as "Initialize BRUSH_Y_X at end of GMC handler"
   (RRG: DP_GUI_MASTER_CNTL, p. 3-175 / PDF 193). */
#define RAGE128_GMC_LD_BRUSH_Y_X     (1u << 31)

/* ------------------------------------------------------------------ */
/* PM4 / CCE command processor (vid_ati_rage128_pm4.c). In 0x700-0x7fc  */
/* the RRG documents only PM4_BUFFER_DL_WPTR_DELAY and the vertex       */
/* controller debug registers; the others are in the CCE 3D supplement  */
/* and in linux r128 DRM r128_drv.h.                                    */
/* ------------------------------------------------------------------ */
#define RAGE128_PM4_BUFFER_OFFSET   0x0700 /* ring base in the 32 MB AGP/PCI address space */
/* PM4_BUFFER_CNTL: PM4_BUFFER_CNTL_FIFO_MODE [31:28] picks how the CCE
   FIFO is split and fed (8 = CCE, vertex cache and indirect buffers all
   by bus mastering); PM4_BUFFER_CNTL_SIZE [5:0] is log2 of the ring size
   in qwords, which is log2 of the size in dwords minus 1 (CCE 3D
   supplement, PM4_BUFFER_CNTL; SDK: Load the CCE Registers, p. 5-5 /
   PDF 101). */
#define RAGE128_PM4_BUFFER_CNTL     0x0704
#define RAGE128_PM4_BUFFER_WM_CNTL  0x0708
#define RAGE128_PM4_BUFFER_DL_RPTR_ADDR 0x070c /* where the CCE writes its read pointer back
                                                  to host memory; the device writes to it as a
                                                  bus address (the CCE 3D supplement calls it a
                                                  32 MB AGP/PCI pointer) */
#define RAGE128_PM4_BUFFER_DL_RPTR  0x0710
#define RAGE128_PM4_BUFFER_DL_WPTR  0x0714 /* write pointer, a dword index into the ring;
                                              drivers submit commands by advancing it */
/* PM4_BUFFER_DL_WPTR_DELAY: a write to PM4_BUFFER_DL_WPTR reaches the
   CCE only after this programmable delay
   (RRG: PM4_BUFFER_DL_WPTR_DELAY, p. 3-218 / PDF 236). */
#define RAGE128_PM4_BUFFER_DL_WPTR_DELAY 0x0718
#define RAGE128_PM4_VC_FPU_SETUP    0x071c
/* PM4_VC_FPU_SETUP culling fields (CCE 3D supplement, PM4_VC_FPU_SETUP;
   SDK: Setting 3D Render States, p. 6-54 / PDF 166). FRONT_DIR: 0 =
   front faces are clockwise, 1 = counterclockwise. BACKFACE_CULLING_FN
   and FRONTFACE_CULLING_FN: 0 = cull, 1 = points, 2 = lines, 3 = solid.
   How the winding maps onto the screen's y-down coordinates is handled
   in vid_ati_rage128_3d.c. */
#define RAGE128_FPU_FRONT_DIR_CCW      (1 << 0)
#define RAGE128_FPU_BACKFACE_SHIFT     1
#define RAGE128_FPU_FRONTFACE_SHIFT    3
#define RAGE128_FPU_FACE_MODE_MASK     3
#define RAGE128_FPU_FACE_CULL          0
#define RAGE128_FPU_FACE_POINTS        1
#define RAGE128_FPU_FACE_LINES         2
#define RAGE128_FPU_FACE_SOLID         3
#define RAGE128_FPU_BACKFACE_SOLID     (3 << 1)
#define RAGE128_FPU_FRONTFACE_SOLID    (3 << 3)
/* PM4_VC_FPU_SETUP shading: PM4_COLOR_FCN [6:5], 0 = solid, 1 = flat,
   2 and 3 = Gouraud; FLAT_SHADE_VERTEX [14], which vertex gives a flat
   triangle its color, 0 = the first (Direct3D), 1 = the last (OpenGL)
   (CCE 3D supplement, PM4_VC_FPU_SETUP; SDK: Setting 3D Render States,
   pp. 6-52-6-53 / PDF 164-165). These are separate from SETUP_CNTL's
   COLOR_FCN [5:3], which the CCE 3D supplement gives for the setup
   engine's own lines and points. */
#define RAGE128_FPU_COLOR_SHIFT        5
#define RAGE128_FPU_COLOR_MASK         3
#define RAGE128_FPU_COLOR_GOURAUD      (2 << 5)
#define RAGE128_FPU_FLAT_VERTEX_OGL    (1 << 14)
/* PM4_VC_FPU_SETUP supersampling: SUPERSAMPLE [11] multiplies the float
   x and y by XFACTOR [12] and YFACTOR [13] (0 = 2, 1 = 4) before they are
   converted to integers (CCE 3D supplement, PM4_VC_FPU_SETUP). */
#define RAGE128_FPU_SUPERSAMPLE        (1 << 11)
#define RAGE128_FPU_XFACTOR_4          (1 << 12)
#define RAGE128_FPU_YFACTOR_4          (1 << 13)
/* Vertex controller debug and status (RRG: PM4_VC_DEBUG_CONFIG,
   p. 3-219 / PDF 237; RRG: PM4_VC_STAT, p. 3-221 / PDF 239). */
#define RAGE128_PM4_VC_DEBUG_CONFIG 0x07a4
#  define RAGE128_VC_DEBUG_DONT_START (1 << 0) /* PM4_VC_DONT_START: the vertex walker never
                                                  starts */
#  define RAGE128_VC_DEBUG_NO_OUTPUT  (1 << 1) /* PM4_VC_NO_OUTPUT: walker output dropped */
#define RAGE128_PM4_VC_STAT         0x07a8 /* read-only */
#define RAGE128_PM4_STAT            0x07b8 /* CCE status; linux r128 DRM r128_cce.c
                                              r128_do_cce_idle polls it */
#  define RAGE128_PM4_STAT_FIFOCNT_MASK 0x00000fff /* PM4_FIFOCNT: free CCE FIFO entries */
#  define RAGE128_PM4_STAT_BUSY         (1 << 16)  /* PM4_BUSY: the CCE is busy */
#  define RAGE128_PM4_STAT_GUI_ACTIVE   (1 << 31)  /* GUI_ACTIVE: OR of the busy bits */
#define RAGE128_PM4_MICROCODE_ADDR  0x07d4
#define RAGE128_PM4_MICROCODE_RADDR 0x07d8
#define RAGE128_PM4_MICROCODE_DATAH 0x07dc
#define RAGE128_PM4_MICROCODE_DATAL 0x07e0
#define RAGE128_PM4_CMDFIFO_ADDR    0x07e4 /* named only in the RRG revision history (RRG:
                                              Revision History, p. B-7 / PDF 321) */
#define RAGE128_PM4_MICRO_CNTL      0x07fc
/* CCE FIFO direct access (SDK: Register Apertures, Table 2-10, p. 2-22 /
   PDF 38). In programmed I/O mode the driver writes packet dwords in
   pairs, the first to PM4_FIFO_DATA_EVEN and the second to
   PM4_FIFO_DATA_ODD, padding an odd count with a type-2 packet (SDK:
   Ring Buffer Server, p. 5-13 / PDF 109). */
#define RAGE128_PM4_FIFO_DATA_EVEN  0x1000
#define RAGE128_PM4_FIFO_DATA_ODD   0x1004

#define RAGE128_AGP_BASE      0x0170 /* AGP aperture base address. The upper 32 MB of the
                                        card's address space maps to AGP_BASE + offset
                                        (RRG: DST_OFFSET, p. 3-137 / PDF 155) */
#define RAGE128_AGP_BASE_MASK 0xffc00000 /* bits 21:0 hardwired 0
                                            (RRG: AGP_BASE, p. 3-186 / PDF 204) */
#define RAGE128_AGP_CNTL      0x0174
#define RAGE128_AGP_CNTL_MASK    0x3fffff3f /* [7:6] and [31:30] reserved */
#define RAGE128_AGP_CNTL_DEFAULT 0x00700000 /* AGP_TG_EXTSENSE, AGP_WRQ_EXTSENSE and
                                               AGP_RD_EXTSENSE reset to 1 (RRG: AGP_CNTL,
                                               pp. 3-186-3-187 / PDF 204-205) */
#define RAGE128_AGP_APER_OFFSET  0x0178 /* read-only 0x02000000, the offset of the AGP image
                                           in the card's address space (RRG:
                                           AGP_APER_OFFSET, p. 3-187 / PDF 205) */
#define RAGE128_PCI_GART_PAGE 0x017c /* [0] PCI_GART_DIS; [31:12] physical address of the
                                        32 KB page table
                                        (RRG: PCI_GART_PAGE, p. 3-207 / PDF 225) */

/* PM4 packet headers: TYPE [31:30] and COUNT [29:16], the number of
   body dwords minus 1. A type-0 header has BASE_INDEX in [10:0], the
   first register's dword index, with [14:11] reserved; a type-3 header
   has IT_OPCODE in [15:8] (SDK: Type-0 CCE Packet, pp. F-3-F-4 /
   PDF 293-294; SDK: Type 3 CCE Packet, p. F-9 / PDF 299). */
#define RAGE128_PM4_TYPE(h)      ((h) >> 30)
#define RAGE128_PM4_COUNT(h)     ((((h) >> 16) & 0x3fff) + 1)
#define RAGE128_PM4_T0_REG(h)    (((h) & 0x7ff) * 4)
#define RAGE128_PM4_T3_OPCODE(h) (((h) >> 8) & 0xff)
/* Type-3 opcodes. The SDK lists the 2D ones with the control-word bit
   0x80 set (PAINT 0x91, BITBLT_MULTI 0x9b and so on; SDK: Summary of the
   CEE Packets, Table 4-13, p. F-10 / PDF 300). The register macros of
   xf86-video-r128 also name the forms with 0x80 clear
   (R128_CCE_PACKET3_PAINT 0x11, R128_CCE_PACKET3_BITBLT_MULTI 0x1b),
   which carry no DP_GUI_MASTER_CNTL dword and draw with the GUI state
   already loaded. */
#define RAGE128_PM4_OP_NOP         0x10
#define RAGE128_PM4_OP_PAINT_NC    0x11 /* PAINT without the control dword; seen in
                                           captured Quake III traffic clearing the
                                           screen */
#define RAGE128_PM4_OP_HOSTROW     0x19
#define RAGE128_PM4_OP_BITBLT_NC   0x1b /* BITBLT_MULTI without the control dword:
                                           the body is bare (src_x_y, dst_x_y, w_h)
                                           triples. The Windows 2000 OpenGL driver
                                           builds it for its per-frame present blit
                                           (RE: atioglaa.dll @69005e1c) */
#define RAGE128_PM4_OP_BLIT_MULTI  0x28
#define RAGE128_PM4_OP_PAINT       0x91
#define RAGE128_PM4_OP_CNTL_BITBLT 0x92 /* R128_CCE_PACKET3_CNTL_BITBLT in
                                           xf86-video-r128 */
#define RAGE128_PM4_OP_BITBLT_MULTI 0x9b /* the Linux DRM's swap blit, back to front
                                            per clip rectangle (linux r128 DRM
                                            r128_state.c r128_cce_dispatch_swap), and
                                            its depth readback
                                            (r128_cce_dispatch_read_span) */
#define RAGE128_PM4_OP_SMALLTEXT   0x93
#define RAGE128_PM4_OP_POLYLINE    0x95
#define RAGE128_PM4_OP_SPANLIST    0x98
#define RAGE128_PM4_OP_PAINT_MULTI 0x9a

/* Type-1 packet: two register writes, REG_INDEX1 in [10:0] and
   REG_INDEX2 in [21:11], each a dword index (SDK: Type 1 CCE Packet,
   p. F-5 / PDF 295). */
#define RAGE128_PM4_T1_REG0(h) (((h) & 0x7ff) * 4)
#define RAGE128_PM4_T1_REG1(h) ((((h) >> 11) & 0x7ff) * 4)

/* Indirect buffers: PM4_IW_INDOFF takes the buffer's offset and
   PM4_IW_INDSIZE its length, an even number of dwords; the INDSIZE write
   starts the fetch (SDK: Indirect Buffer, p. 5-15 / PDF 111). The Linux
   DRM writes both in one type-0 packet (linux r128 DRM r128_state.c
   r128_cce_dispatch_indirect). */
#define RAGE128_PM4_IW_INDOFF  0x0738
#define RAGE128_PM4_IW_INDSIZE 0x073c
/* PM4_IW_INDSIZE [22:0], the length in dwords (CCE 3D supplement,
   PM4_IW_INDSIZE). */
#define RAGE128_PM4_IW_INDSIZE_MASK 0x007fffffu

/* ------------------------------------------------------------------ */
/* 3D engine (vid_ati_rage128_3d.c). The _C registers at 0x1c80-0x1d44  */
/* are in the CCE 3D supplement; a few also have RRG pages, cited where */
/* used.                                                                */
/* ------------------------------------------------------------------ */
#define RAGE128_SCALE_3D_CNTL        0x1a00 /* dither, 3D function, blend factors, alpha
                                               test, fog (CCE 3D supplement,
                                               SCALE_3D_CNTL) */
/* COMPOSITE_SHADOW_ID (RRG: COMPOSITE_SHADOW_ID, p. 3-145 / PDF 163). */
#define RAGE128_COMPOSITE_SHADOW_ID  0x1a0c
#  define RAGE128_SHADOW_ID_MASK          0x00ffffff /* [23:0] count of 3D primitives
                                                        executed */
#  define RAGE128_SHADOW_AUTO_INC_DIS     (1 << 27)  /* 1 = the count does not advance */
#  define RAGE128_SHADOW_ID_WMASK         0x08ffffff /* [26:24] and [31:28] reserved */
/* Table fog: a 256-entry table of 8-bit fog factors indexed by the
   pixel's interpolated z. The final color is f * pixel + (1 - f) * fog
   color, so 255 leaves the pixel unfogged. Software writes the starting
   index to FOG_TABLE_INDEX, then the entries to FOG_TABLE_DATA, and the
   index advances after each write (SDK: Setting 3D Render States,
   pp. 6-51-6-52 / PDF 163-164). In captured Windows 98 Direct3D traffic
   the driver loads the table itself and never writes
   FOG_3D_TABLE_START, FOG_3D_TABLE_END or FOG_3D_TABLE_DENSITY
   (0x1810-0x1818), which RRG Rev 1.01 lists as removed (RRG: Revision
   History, p. B-8 / PDF 322). */
#define RAGE128_FOG_TABLE_INDEX      0x1a14
#define RAGE128_FOG_TABLE_DATA       0x1a18
/* SCALE_3D_DATATYPE has no register page. The CCE 3D supplement gives
   parts of it through aliases in PRIM_TEX_CNTL_C and SEC_TEX_CNTL_C, and
   the SDK's scaled blit writes the destination pixel depth code into it
   (SDK: Scaled Block Transfer, p. 4-12 / PDF 86). */
#define RAGE128_SCALE_3D_DATATYPE    0x1a20
#define RAGE128_CLR_CMP_CLR_3D       0x1a24
#define RAGE128_CLR_CMP_MSK_3D       0x1a28
/* The older, non-_C combine registers. The RRG names
   PRIMARY_TEXTURE_COMBINE_CNTL (0x1a08) and SECONDARY_TEXTURE_COMBINE_CNTL
   (0x1a34) only in its revision history
   (RRG: Revision History, p. B-7 / PDF 321), and has a page for
   CONSTANT_COLOR (0x1a30). In captured
   Windows 98 Direct3D traffic (3DMark99) the driver programs the second
   texture stage's combine through 0x1a34, not SEC_TEX_COMBINE_CNTL_C
   (0x1d04), so the device takes that stage from 0x1a34. 0x1a08 and
   0x1a30 have not been seen in traffic and are not modeled as aliases. */
#define RAGE128_SECONDARY_TEXTURE_COMBINE_CNTL 0x1a34
#define RAGE128_SETUP_CNTL           0x1bc4 /* shade source, primitive type, texture ST
                                               format, subpixel amount (CCE 3D
                                               supplement, SETUP_CNTL) */
#define RAGE128_WINDOW_XY_OFFSET     0x1bcc /* added to every vertex x and y
                                               (RRG: WINDOW_XY_OFFSET, p. 3-249 / PDF 267) */
#define RAGE128_DRAW_LINE_POINT      0x1bd0 /* point and line control for the setup
                                               engine (RRG: DRAW_LINE_POINT,
                                               p. 3-249 / PDF 267); the device takes 3D
                                               work only through the CCE and ignores
                                               writes here */
#define RAGE128_SETUP_CNTL_PM4       0x1bd4 /* named only in the RRG revision history; no
                                               public document gives its fields */
#define RAGE128_DST_PITCH_OFFSET_C   0x1c80 /* first register of the CCE 3D context block,
                                               same packing as DST_PITCH_OFFSET
                                               (RRG: DST_PITCH_OFFSET_C, p. 3-145 / PDF 163) */
#define RAGE128_DP_GUI_MASTER_CNTL_C 0x1c84 /* alias of DP_GUI_MASTER_CNTL (0x146c) */
#define RAGE128_SC_TOP_LEFT_C        0x1c88 /* alias of SC_TOP_LEFT
                                               (RRG: SC_TOP_LEFT_C, p. 3-162 / PDF 180) */
#define RAGE128_SC_BOTTOM_RIGHT_C    0x1c8c /* alias of SC_BOTTOM_RIGHT */
#define RAGE128_Z_OFFSET_C           0x1c90 /* depth buffer offset in bytes */
#define RAGE128_Z_PITCH_C            0x1c94 /* Z_PITCH [9:0] in units of 8 pixels, Z_TILE
                                               [16] */
#define RAGE128_Z_STEN_CNTL_C        0x1c98 /* RRG Rev 0.03 corrected its address from
                                               0x1ac4, where Z_OFFSET is (RRG: Revision
                                               History, pp. B-6-B-7 / PDF 320-321) */
#define RAGE128_TEX_CNTL_C           0x1c9c /* the 3D enable bits: Z, stencil, texture,
                                               alpha, fog and others */
#define RAGE128_MISC_3D_STATE_CNTL   0x1ca0 /* alpha reference, blend factors, fog table
                                               select (RRG: MISC_3D_STATE_CNTL_REG,
                                               p. 3-256 / PDF 274) */
#define RAGE128_TEX_CLR_CMP_CLR_C    0x1ca4
#define RAGE128_TEX_CLR_CMP_MSK_C    0x1ca8
#define RAGE128_FOG_COLOR_C          0x1cac /* blue [7:0], green [15:8], red [23:16] */
#define RAGE128_PRIM_TEX_CNTL_C      0x1cb0
#define RAGE128_PRIM_TEX_COMBINE_CNTL_C 0x1cb4
#define RAGE128_TEX_SIZE_PITCH_C     0x1cb8
#define RAGE128_PRIM_TEX_OFFSET_C(n) (0x1cbc + (n) * 4) /* mip level n = 0..10 */
#define RAGE128_SEC_TEX_CNTL_C       0x1d00
#define RAGE128_SEC_TEX_COMBINE_CNTL_C 0x1d04
#define RAGE128_SEC_TEX_OFFSET_C(n)  (0x1d08 + (n) * 4) /* mip level n = 0..10 */
#define RAGE128_CONSTANT_COLOR_C     0x1d34
#define RAGE128_PRIM_TEX_BORDER_COLOR_C 0x1d38
#define RAGE128_SEC_TEX_BORDER_COLOR_C  0x1d3c
#define RAGE128_STEN_REF_MASK_C      0x1d40 /* STEN_REF_MSK_C: reference [7:0], compare
                                               mask [23:16], write mask [31:24] */
#define RAGE128_STEN_REF_MASK_LEGACY 0x1ad0 /* in no public document. Captured Windows 98
                                               Direct3D traffic writes the stencil
                                               reference here in [7:0], so it is modeled
                                               with the STEN_REF_MSK_C layout and state */
#define RAGE128_PLANE_3D_MASK_C      0x1d44 /* "would be written into DP_WRITE_MASK"
                                               (CCE 3D supplement, PLANE_3D_MASK_C) */

/* Texture palette for 8 bpp pseudocolor textures (datatype 2). The RRG
   has no page for these registers; its revision history names only
   TEX_PALETTE_WR_INDEX (RRG: Revision History, p. B-3 / PDF 317). The
   offsets come from captured Windows 98 Direct3D traffic (Lunatik): the
   driver writes the starting entry to the index register's [7:0], with
   bit 12 set (0x1000 for entry 0), then streams 256 entries of
   0x00RRGGBB to the data register, which advances the entry after each
   write. */
#define RAGE128_TEX_PALETTE_INDEX    0x1968
#define RAGE128_TEX_PALETTE_DATA     0x196c

/* Front-end scaler. The five secondary scaler registers at 0x1980-0x1990
   have RRG pages (RRG: SECONDARY_SCALE_PITCH to SECONDARY_SCALE_VACC,
   pp. 3-160-3-161 / PDF 178-179). The registers at 0x1994-0x19b4
   appear only in the list of registers added in RRG Rev 0.03, with no
   fields (RRG: Revision History, pp. B-4-B-5 / PDF 318-319). That list
   gives SCALE_OFFSET_0 and SCALE_PITCH the same address, 0x1998; ATI's
   motion-compensation driver writes the surface base to 0x1998 and the
   pitch >> 3 to 0x199c, and packs SCALE_SCR_HEIGHT_WIDTH and
   SCALE_DST_HEIGHT_WIDTH as (height << 16) | width (RE: ATIMCRAA.DLL
   @b01103a0). */
#define RAGE128_SECONDARY_SCALE_PITCH  0x1980 /* [8:0] pitch in units of 8 pixels */
#define RAGE128_SECONDARY_SCALE_X_INC  0x1984
#define RAGE128_SECONDARY_SCALE_Y_INC  0x1988
#define RAGE128_SECONDARY_SCALE_HACC   0x198c
#define RAGE128_SECONDARY_SCALE_VACC   0x1990
#define RAGE128_SCALE_SCR_HEIGHT_WIDTH 0x1994
#define RAGE128_SCALE_OFFSET_0         0x1998
#define RAGE128_SCALE_PITCH            0x199c /* pitch >> 3 */
#define RAGE128_SCALE_X_INC            0x19a0
#define RAGE128_SCALE_Y_INC            0x19a4
#define RAGE128_SCALE_HACC             0x19a8
#define RAGE128_SCALE_VACC             0x19ac
#define RAGE128_SCALE_DST_X_Y          0x19b0
#define RAGE128_SCALE_DST_HEIGHT_WIDTH 0x19b4

/* MPEG-2 motion compensation. The RRG gives field names and bit ranges
   for all four control registers (RRG: MC_SRC1_CNTL, MC_SRC2_CNTL and
   MC_DST_CNTL, p. 3-261 / PDF 279; RRG: MC_START_CNTL, p. 3-262 /
   PDF 280) but describes none of them beyond the on/off values of
   SECONDARY_TEX_EN and ALPHA_EN. The meanings below come
   from ATI's motion-compensation driver, which carries a software model
   of the block with decoders that split each register value back into
   its fields (RE: ATIMCRAA.DLL @b0102770). The reference frames are
   bound through the legacy texture offset slots below, so motion
   compensation is a pass of the scaler rather than a separate engine. A
   write to MC_START_CNTL starts the block; the four registers keep their
   values between blocks. */
#define RAGE128_MC_SRC2_CNTL         0x19d4 /* SECONDARY_SCALE_HACC [12:0],
                                               SECONDARY_SCALE_VACC [27:16],
                                               SECONDARY_SCALE_PITCH_ADJ [31:30] */
#define RAGE128_MC_SRC1_CNTL         0x19d8 /* SCALE_HACC [12:0], SCALE_VACC [27:16],
                                               IDCT_EN [28], SECONDARY_TEX_EN [29],
                                               SCALE_PITCH_ADJ [31:30] */
#define RAGE128_MC_DST_CNTL          0x19dc /* DST_Y [13:0], DST_X [29:16],
                                               DST_PITCH_ADJ [31:30] */
#define RAGE128_MC_START_CNTL        0x19e0
#  define RAGE128_MC_SRC1_IDCT_EN         (1 << 28)
#  define RAGE128_MC_SRC1_SECONDARY_TEX   (1 << 29)
/* The _PITCH_ADJ fields multiply the pitch: 0 -> 1, 1 -> 2, 2 -> 4, 3 is
   reserved (RRG: SECONDARY_SCALE_PITCH, p. 3-160 / PDF 178; the driver's
   decoder maps 3 to 0). */
#define RAGE128_MC_PITCH_ADJ(v)      (((v) >> 30) & 3)
/* MC_START_CNTL, which the RRG divides into SCALE_OFFSET_PTR [3:0],
   DST_OFFSET [24:4], ALPHA_EN [25], SECONDARY_OFFSET_PTR [28:26] and
   DST_HEIGHT_WIDTH [31:29]. In the driver's decoder the second slot
   index is stored plus one, so 0 means no second reference; the block
   size codes 0, 1, 5 and 6 mean 8x4, 8x8, 16x8 and 16x16 and the other
   codes are invalid. Bit 25 is ALPHA_EN in the RRG and
   RAGE128_MC_START_IDCT_SRC here: the driver sets it only for blocks
   with an IDCT residual, together with a ONE + ONE blend in
   SCALE_3D_CNTL that adds the residual to the prediction (RE:
   ATIMCRAA.DLL @b010f530). */
#define RAGE128_MC_START_SRC1_SLOT(v) ((v) & 0xf)
#define RAGE128_MC_START_DST_ADDR(v)  ((v) & 0x01fffff0)
#define RAGE128_MC_START_IDCT_SRC     (1 << 25)
#define RAGE128_MC_START_SRC2_SLOT(v) ((((v) >> 26) & 7) - 1)
#define RAGE128_MC_START_BLK_SIZE(v)  (((v) >> 29) & 7)

/* M2IA, the authentication port that gates MPEG-2 decode. No public
   document describes it. Two driver binaries that share no code
   program it the same way: ATIMIAXX.DLL, whose exported functions
   ATIMIA_InitAuthentication, ATIMIA_Authentication1,
   ATIMIA_Authentication2 and ATIMIA_EndAuthentication run the exchange
   (RE: ATIMIAXX.DLL @10001a70, the first of them), and the DirectDraw
   code of the ATI3DRAA display driver. The status register (0x1f88)
   reads a state code; the data register (0x1f8c) carries the challenge
   and response words. */
#define RAGE128_M2IA_STATUS          0x1f88
#define RAGE128_M2IA_DATA            0x1f8c

/* The older, non-_C 3D enables and texture offset arrays, counterparts
   of TEX_CNTL_C, PRIM_TEX_0_OFFSET_C and SEC_TEX_0_OFFSET_C. The RRG
   names TEX_CNTL (0x1800) only in its revision history (RRG: Revision
   History, p. B-7 / PDF 321) and does not name the offset arrays. The
   motion-compensation driver writes 0x800200 to TEX_CNTL for IDCT
   blocks, which in the TEX_CNTL_C layout is ALPHA_EN [9] and
   TEX_CACHE_FLUSH [23] (CCE 3D supplement, TEX_CNTL_C); the device
   assumes the two registers share that layout, which fits but is not
   proven. The driver gives each reference surface one offset slot and
   selects the slots from MC_START_CNTL; its decoder accepts a first
   slot index up to 11 and a second up to 5 (RE: ATIMCRAA.DLL
   @b0104370). */
#define RAGE128_TEX_CNTL             0x1800
#define RAGE128_PRIM_TEX_OFFSET(n)   (0x1840 + (n) * 4) /* n = 0..10 */
#define RAGE128_SEC_TEX_OFFSET(n)    (0x1880 + (n) * 4) /* n = 0..10 */

/* 3D type-3 opcodes. The SDK describes 3D_RNDR_GEN_PRIM (0x25),
   3D_RNDR_GEN_INDX_PRIM (0x23) and LOAD_PALETTE (0x2c) in its Appendix F
   and lists PURGE (0x2d) and NEXT_VERTEX_BUNDLE (0x2e) in its packet
   summary (SDK: Summary of the CEE Packets, Table 4-13, pp. F-10-F-11 /
   PDF 300-301). 0x20 and 0x21 appear only as macro names in
   xf86-video-r128 (R128_CCE_PACKET3_3D_SAVE_CONTEXT and
   R128_CCE_PACKET3_3D_PLAY_CONTEXT). */
#define RAGE128_PM4_OP_3D_SAVE_CONTEXT       0x20
#define RAGE128_PM4_OP_3D_PLAY_CONTEXT       0x21
#define RAGE128_PM4_OP_3D_RNDR_GEN_INDX_PRIM 0x23
#define RAGE128_PM4_OP_3D_RNDR_GEN_PRIM      0x25
#define RAGE128_PM4_OP_LOAD_PALETTE          0x2c
#define RAGE128_PM4_OP_PURGE                 0x2d /* "Purge the pixel cache"; the device
                                                     models no pixel cache, so it accepts
                                                     and skips the packet */
#define RAGE128_PM4_OP_NEXT_VERTEX_BUNDLE    0x2e

/* One MPEG-2 macroblock for the IDCT and motion compensation path: a
   mode word, then a list of records that write the MC control registers
   and carry the run-level DCT coefficients of the six blocks
   (vid_ati_rage128_mpeg.c). No public document names this opcode; the
   format is that of the parser in ATI's software model of the block
   (RE: ATIMCRAA.DLL @b0102770). */
#define RAGE128_PM4_OP_MPEG_MB               0x31

/* VC_CNTL, the control dword of a 3D draw packet: VC_PRIM_TYPE [3:0],
   PRIM_WALK [5:4] (1 = indices, 2 = vertex list, 3 = vertices inline in
   the ring) and NUM_VERTEX [31:16] (SDK: VC_CNTL, Table F-44, p. F-51 /
   PDF 341). */
#define RAGE128_VC_PRIM_TYPE(v) ((v) & 0xf) /* 1=point 2=line 3=polyline 4=tri list 5=fan 6=strip 7=type2 */
#define RAGE128_VC_PRIM_WALK(v) (((v) >> 4) & 0x3)
#define RAGE128_VC_NUM(v)       ((v) >> 16) /* vertex count; for an indexed draw the index
                                               count (linux r128 DRM r128_state.c
                                               r128_cce_dispatch_indices) */
#define RAGE128_VC_WALK_IND  1
#define RAGE128_VC_WALK_LIST 2
#define RAGE128_VC_WALK_RING 3

/* VC_FORMAT: which fields each vertex carries. Every vertex starts with
   x, y and z as floats; each set flag adds its field, in the order of
   the flags below (SDK: VC_FORMAT, Table F-43, p. F-50 / PDF 340; SDK:
   FTLVERTEX, Table F-45, pp. F-51-F-53 / PDF 341-343). */
#define RAGE128_VCF_RHW          0x001 /* 1/w, float */
#define RAGE128_VCF_DIFFUSE_BGR  0x002 /* diffuse blue, green, red, three floats */
#define RAGE128_VCF_DIFFUSE_A    0x004 /* diffuse alpha, float */
#define RAGE128_VCF_DIFFUSE_ARGB 0x008 /* diffuse ARGB, one packed dword */
#define RAGE128_VCF_SPEC_BGR     0x010 /* specular blue, green, red, three floats */
#define RAGE128_VCF_SPEC_F       0x020 /* specular fog, float */
#define RAGE128_VCF_SPEC_FRGB    0x040 /* specular fog and RGB, one packed dword */
#define RAGE128_VCF_S_T          0x080 /* first texture s, t, two floats */
#define RAGE128_VCF_S2_T2        0x100 /* second texture s, t, two floats */
#define RAGE128_VCF_RHW2         0x200 /* 1/w for the second texture, float, last */
/* clang-format on */

#endif /*VIDEO_ATI_RAGE128_REGS_H*/
