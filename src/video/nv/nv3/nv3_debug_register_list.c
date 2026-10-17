/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 debug register list
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/rom.h> 
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

//#ifdef ENABLE_NV_LOG 
nv_register_t nv3_registers[] = {
    { NV3_PBUS_DEBUG_0, "PBUS - Debug Register", NULL, NULL},
    { NV3_PBUS_INTR, "PBUS - Interrupt Status", NULL, NULL},
    { NV3_PBUS_INTR_EN, "PBUS - Interrupt Enable", NULL, NULL,},
    { NV3_PFB_BOOT, "PFB Boot Config", NULL, NULL},
    { NV3_PFB_DELAY, "PFB Delay", NULL, NULL},
    { NV3_PFB_DEBUG_0, "PFB Debug", NULL, NULL },
    { NV3_PFB_GREEN_0, "PFB Green / Power Saving", NULL, NULL,},
    { NV3_PFB_CONFIG_0, "PFB Framebuffer Config 0", NULL, nv3_pfb_config0_write },
    { NV3_PFB_CONFIG_1, "PFB Framebuffer Config 1", NULL, NULL },
    { NV3_PFB_RTL, "PFB RTL (Part of memory timings)", NULL, NULL },
    { NV3_PFIFO_INTR, "PFIFO - Interrupt Status", NULL, NULL},
    { NV3_PFIFO_INTR_EN, "PFIFO - Interrupt Enable", NULL, NULL,},
    { NV3_PFIFO_DELAY_0, "PFIFO - DMA Delay/Retry Register", NULL, NULL},
    { NV3_PFIFO_DEBUG_0, "PFIFO - Debug 0", NULL, NULL, }, 
    { NV3_PFIFO_CONFIG_0, "PFIFO - Config 0", NULL, NULL, },
    { NV3_PFIFO_CONFIG_RAMFC, "PFIFO - RAMIN RAMFC Config", NULL, NULL },
    { NV3_PFIFO_CONFIG_RAMHT, "PFIFO - RAMIN RAMHT Config", NULL, NULL },
    { NV3_PFIFO_CONFIG_RAMRO, "PFIFO - RAMIN RAMRO Config", NULL, NULL },
    { NV3_PFIFO_CACHE_REASSIGNMENT, "PFIFO - Allow Cache Channel Reassignment", NULL, NULL },
    { NV3_PFIFO_CACHE0_PULL0, "PFIFO - Cache0 Puller Control", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULL0, "PFIFO - Cache1 Puller Control", NULL, NULL },
    { NV3_PFIFO_CACHE0_PULLER_CTX_STATE, "PFIFO - Cache0 Puller State1 (Is context clean?)", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULL0, "PFIFO - Cache1 Puller State0", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULLER_CTX_STATE, "PFIFO - Cache1 Puller State1 (Is context clean?)", NULL, NULL},
    { NV3_PFIFO_CACHE0_PUSH_ENABLED, "PFIFO - Cache0 Access", NULL, NULL, },
    { NV3_PFIFO_CACHE1_PUSH_ENABLED, "PFIFO - Cache1 Access", NULL, NULL, },
    { NV3_PFIFO_CACHE0_PUSH_CHANNEL_ID, "PFIFO - Cache0 Push Channel ID", NULL, NULL, },
    { NV3_PFIFO_CACHE1_PUSH_CHANNEL_ID, "PFIFO - Cache1 Push Channel ID", NULL, NULL, },
    { NV3_PFIFO_DEBUG_0, "PFIFO - Cache0/1 DMA Error Pending?", NULL, NULL, },
    { NV3_PFIFO_CACHE0_STATUS, "PFIFO - Cache0 Status", NULL, NULL},
    { NV3_PFIFO_CACHE1_STATUS, "PFIFO - Cache1 Status", NULL, NULL}, 
    { NV3_PFIFO_CACHE0_GET, "PFIFO - Cache0 Get", NULL, NULL },
    { NV3_PFIFO_CACHE0_CTX, "PFIFO - Cache0 Context", NULL, NULL },
    { NV3_PFIFO_CACHE1_GET, "PFIFO - Cache1 Get", NULL, NULL },
    { NV3_PFIFO_CACHE0_PUT, "PFIFO - Cache0 Put", NULL, NULL },
    { NV3_PFIFO_CACHE1_PUT, "PFIFO - Cache1 Put", NULL, NULL },
    //Cache1 exclusive stuff
    { NV3_PFIFO_CACHE1_DMA_CONFIG_0, "PFIFO - Cache1 DMA Access (bit 0: is running, bit 4: is busy)", NULL, NULL },
    { NV3_PFIFO_CACHE1_DMA_CONFIG_1, "PFIFO - Cache1 DMA Length", NULL, NULL },
    { NV3_PFIFO_CACHE1_DMA_CONFIG_2, "PFIFO - Cache1 DMA Address", NULL, NULL },
    { NV3_PFIFO_CACHE1_DMA_CONFIG_3, "PFIFO - Cache1 DMA Target Node", NULL, NULL },
    { NV3_PFIFO_CACHE1_DMA_STATUS, "PFIFO - Cache1 DMA Status", NULL, NULL },
    { NV3_PFIFO_CACHE1_DMA_TLB_PT_BASE, "PFIFO - Cache1 DMA TLB - Pagetable Base", NULL, NULL },
    { NV3_PFIFO_CACHE1_DMA_TLB_PTE, "PFIFO - Cache1 DMA TLB - Pagetable Entry (31:12 - Frame Address; bit 0 - Is Present)", NULL, NULL },
    { NV3_PFIFO_CACHE1_DMA_TLB_TAG, "PFIFO - Cache1 DMA TLB - Tag", NULL, NULL },
    //Runout
    { NV3_PFIFO_RUNOUT_GET, "PFIFO Runout Get Address [8:3 if 512b, otherwise 12:3]", NULL, NULL },
    { NV3_PFIFO_RUNOUT_PUT, "PFIFO Runout Put Address [8:3 if 512b, otherwise 12:3]", NULL, NULL },
    { NV3_PFIFO_RUNOUT_STATUS, "PFIFO Runout Status", NULL, NULL },
    { NV3_PGRAPH_DEBUG_0, "PGRAPH Debug 0", NULL, NULL },
    { NV3_PGRAPH_DEBUG_1, "PGRAPH Debug 1", NULL, NULL },
    { NV3_PGRAPH_DEBUG_2, "PGRAPH Debug 2", NULL, NULL },
    { NV3_PGRAPH_DEBUG_3, "PGRAPH Debug 3", NULL, NULL },
    { NV3_PGRAPH_INTR_0, "PGRAPH Interrupt Status 0", NULL, NULL },
    { NV3_PGRAPH_INTR_EN_0, "PGRAPH Interrupt Enable 0", NULL, NULL },
    { NV3_PGRAPH_INTR_1, "PGRAPH Interrupt Status 1", NULL, NULL },
    { NV3_PGRAPH_INTR_EN_1, "PGRAPH Interrupt Enable 1", NULL, NULL },
    { NV3_PGRAPH_CTX_SWITCH, "PGRAPH DMA Context Switch", NULL, NULL },
    { NV3_PGRAPH_CONTEXT_CONTROL, "PGRAPH DMA Context Control", NULL, NULL },
    { NV3_PGRAPH_CONTEXT_USER, "PGRAPH DMA Context User", NULL, NULL },
    //{ NV3_PGRAPH_CONTEXT_CACHE(0), "PGRAPH DMA Context Cache", NULL, NULL },
    { NV3_PGRAPH_ABS_UCLIP_XMIN, "PGRAPH Absolute Clip Minimum X [17:0]", NULL, NULL },
    { NV3_PGRAPH_ABS_UCLIP_XMAX, "PGRAPH Absolute Clip Maximum X [17:0]", NULL, NULL },
    { NV3_PGRAPH_ABS_UCLIP_YMIN, "PGRAPH Absolute Clip Minimum Y [17:0]", NULL, NULL },
    { NV3_PGRAPH_ABS_UCLIP_YMAX, "PGRAPH Absolute Clip Maximum Y [17:0]", NULL, NULL },
    { NV3_PGRAPH_SRC_CANVAS_MIN, "PGRAPH Source Canvas Minimum Coordinates (Bits 30:16 = Y, Bits 10:0 = X)", NULL, NULL},
    { NV3_PGRAPH_SRC_CANVAS_MAX, "PGRAPH Source Canvas Maximum Coordinates (Bits 30:16 = Y, Bits 10:0 = X)", NULL, NULL},
    { NV3_PGRAPH_DST_CANVAS_MIN, "PGRAPH Destination Canvas Minimum Coordinates (Bits 30:16 = Y, Bits 10:0 = X)", NULL, NULL},
    { NV3_PGRAPH_DST_CANVAS_MAX, "PGRAPH Destination Canvas Maximum Coordinates (Bits 30:16 = Y, Bits 10:0 = X)", NULL, NULL},
    { NV3_PGRAPH_PATTERN_COLOR_0_RGB, "PGRAPH Pattern Color 0_0 (Bits 29:20 = Red, Bits 19:10 = Green, Bits 9:0 = Blue)", NULL, NULL, },
    { NV3_PGRAPH_PATTERN_COLOR_0_ALPHA, "PGRAPH Pattern Color 0_1 (Bits 7:0 = Alpha)", NULL, NULL, },
    { NV3_PGRAPH_PATTERN_COLOR_1_RGB, "PGRAPH Pattern Color 1_0 (Bits 29:20 = Red, Bits 19:10 = Green, Bits 9:0 = Blue)", NULL, NULL, },
    { NV3_PGRAPH_PATTERN_COLOR_1_ALPHA, "PGRAPH Pattern Color 1_1 (Bits 7:0 = Alpha)", NULL, NULL, },
    { NV3_PGRAPH_PATTERN_BITMAP_0, "PGRAPH Pattern Bitmap (High 32bits)", NULL, NULL},
    { NV3_PGRAPH_PATTERN_BITMAP_1, "PGRAPH Pattern Bitmap (Low 32bits)", NULL, NULL},
    { NV3_PGRAPH_PATTERN_SHAPE, "PGRAPH Pattern Shape (1:0 - 0=8x8, 1=64x1, 2=1x64)", NULL, NULL},
    { NV3_PGRAPH_ROP3, "PGRAPH GDI Ternary Render Operation ROP3 (2^3 bits = 256 possible operations)", NULL, NULL},
    { NV3_PGRAPH_PLANE_MASK, "PGRAPH Current Plane Mask (7:0)", NULL, NULL},
    { NV3_PGRAPH_CHROMA_KEY, "PGRAPH Chroma Key (17:0) (Bit 30 = Alpha, 29:20 = Red, 19:10 = Green, 9:0 = Blue)", NULL, NULL},
    { NV3_PGRAPH_BETA, "PGRAPH Beta factor", NULL, NULL },
    { NV3_PGRAPH_DMA, "PGRAPH DMA", NULL, NULL },
    { NV3_PGRAPH_CLIP_MISC, "PGRAPH Clipping Miscellaneous Settings", NULL, NULL },
    { NV3_PGRAPH_NOTIFY, "PGRAPH Notifier (Wip...)", NULL, NULL },
    { NV3_PGRAPH_CLIP0_MIN, "PGRAPH Clip0 Min (Bits 30:16 = Y, Bits 10:0 = X)", NULL, NULL},
    { NV3_PGRAPH_CLIP0_MAX, "PGRAPH Clip0 Max (Bits 30:16 = Y, Bits 10:0 = X)", NULL, NULL},
    { NV3_PGRAPH_CLIP1_MIN, "PGRAPH Clip1 Min (Bits 30:16 = Y, Bits 10:0 = X)", NULL, NULL},
    { NV3_PGRAPH_CLIP1_MAX, "PGRAPH Clip1 Max (Bits 30:16 = Y, Bits 10:0 = X)", NULL, NULL},
    { NV3_PGRAPH_FIFO_ACCESS, "PGRAPH - Can we access PFIFO?", NULL, NULL, },
    { NV3_PGRAPH_STATUS, "PGRAPH Status", NULL, NULL },
    { NV3_PGRAPH_TRAPPED_ADDRESS, "PGRAPH Trapped Address", NULL, NULL },
    { NV3_PGRAPH_TRAPPED_DATA, "PGRAPH Trapped Data", NULL, NULL },
    { NV3_PGRAPH_INSTANCE, "PGRAPH Object Instance", NULL, NULL},
    { NV3_PGRAPH_TRAPPED_INSTANCE, "PGRAPH Trapped Object Instance", NULL, NULL },
    { NV3_PGRAPH_DMA_INTR_0, "PGRAPH DMA Interrupt Status (unimplemented)", NULL, NULL },
    { NV3_PGRAPH_DMA_INTR_EN_0, "PGRAPH DMA Interrupt Enable (unimplemented)", NULL, NULL },
    { NV3_PMC_BOOT, "PMC: Boot Manufacturing Information", NULL, NULL },
    { NV3_PMC_INTR, "PMC: Current Pending Subsystem Interrupts", NULL, NULL},
    { NV3_PMC_INTR_EN, "PMC: Global Interrupt Enable", NULL, NULL,},
    { NV3_PMC_ENABLE, "PMC: Global Subsystem Enable", NULL, NULL },
    { NV3_PME_INTR, "PME - Interrupt Status", NULL, NULL},
    { NV3_PME_INTR_EN, "PME - Interrupt Enable", NULL, NULL,},
    { NV3_PRAMDAC_CURSOR_START, "PRAMDAC - Cursor Start Position", NULL, NULL },
    { NV3_PRAMDAC_CLOCK_PIXEL, "PRAMDAC - NV3 GPU Core - Pixel clock", nv3_pramdac_get_pixel_clock_register, nv3_pramdac_set_pixel_clock_register },
    { NV3_PRAMDAC_CLOCK_MEMORY, "PRAMDAC - NV3 GPU Core - Memory clock", nv3_pramdac_get_core_clock_register, nv3_pramdac_set_core_clock_register },
    { NV3_PRAMDAC_COEFF_SELECT, "PRAMDAC - PLL Clock Coefficient Select", NULL, NULL},
    { NV3_PRAMDAC_GENERAL_CONTROL, "PRAMDAC - General Control", NULL, NULL },
    { NV3_PRAMDAC_VSERR_WIDTH, "PRAMDAC - Vertical Sync Error Width", NULL, NULL},
    { NV3_PRAMDAC_VEQU_END, "PRAMDAC - VEqu End", NULL, NULL},
    { NV3_PRAMDAC_VBBLANK_START, "PRAMDAC - VBBlank Start", NULL, NULL},
    { NV3_PRAMDAC_VBBLANK_END, "PRAMDAC - VBBlank End", NULL, NULL},
    { NV3_PRAMDAC_HBLANK_END, "PRAMDAC - Horizontal Blanking Interval End", NULL, NULL},
    { NV3_PRAMDAC_HBLANK_START, "PRAMDAC - Horizontal Blanking Interval Start", NULL, NULL},
    { NV3_PRAMDAC_VBLANK_END, "PRAMDAC - Vertical Blanking Interval End", NULL, NULL},
    { NV3_PRAMDAC_VBLANK_START, "PRAMDAC - Vertical Blanking Interval Start", NULL, NULL},
    { NV3_PRAMDAC_VEQU_START, "PRAMDAC - VEqu Start", NULL, NULL},
    { NV3_PRAMDAC_VTOTAL, "PRAMDAC - Total Vertical Lines", NULL, NULL},
    { NV3_PRAMDAC_HSYNC_WIDTH, "PRAMDAC - Horizontal Sync Pulse Width", NULL, NULL},
    { NV3_PRAMDAC_HBURST_START, "PRAMDAC - Horizontal Burst Signal Start", NULL, NULL},
    { NV3_PRAMDAC_HBURST_END, "PRAMDAC - Horizontal Burst Signal Start", NULL, NULL},
    { NV3_PRAMDAC_HTOTAL, "PRAMDAC - Total Horizontal Lines", NULL, NULL},
    { NV3_PRAMDAC_HEQU_WIDTH, "PRAMDAC - HEqu End", NULL, NULL},
    { NV3_PRAMDAC_HSERR_WIDTH, "PRAMDAC - Horizontal Sync Error", NULL, NULL},
    { NV3_USER_DAC_PIXEL_MASK, "PRAMDAC - User DAC Pixel Mask", NULL, NULL},
    { NV3_USER_DAC_READ_MODE_ADDRESS, "PRAMDAC - User DAC Read Mode Address", NULL, NULL},
    { NV3_USER_DAC_WRITE_MODE_ADDRESS, "PRAMDAC - User DAC Write Mode Address", NULL, NULL},
    { NV3_USER_DAC_PALETTE_DATA, "PRAMDAC - User DAC Palette Data", NULL, NULL},
    { NV3_PSTRAPS, "Straps - OEM Chip Configuration", NULL, NULL },
    { NV3_PTIMER_INTR, "PTIMER - Interrupt Status", NULL, NULL},
    { NV3_PTIMER_INTR_EN, "PTIMER - Interrupt Enable", NULL, NULL,},
    { NV3_PTIMER_NUMERATOR, "PTIMER - Numerator", NULL, NULL, },
    { NV3_PTIMER_DENOMINATOR, "PTIMER - Denominator", NULL, NULL, },
    { NV3_PTIMER_TIME_0_NSEC, "PTIMER - Time0", NULL, NULL, },
    { NV3_PTIMER_TIME_1_NSEC, "PTIMER - Time1", NULL, NULL, },
    { NV3_PTIMER_ALARM_NSEC, "PTIMER - Alarm", NULL, NULL, },
    { NV3_PVIDEO_INTR, "PVIDEO - Interrupt Status", NULL, NULL},
    { NV3_PVIDEO_INTR_EN, "PVIDEO - Interrupt Enable", NULL, NULL,},
    { NV3_PVIDEO_FIFO_THRESHOLD, "PVIDEO - FIFO Fill Threshold", NULL, NULL},
    { NV3_PVIDEO_FIFO_BURST_LENGTH, "PVIDEO - FIFO Burst Length (1=32, 2=64, 3=128)", NULL, NULL},
    { NV3_PVIDEO_OVERLAY, "PVIDEO - Overlay Info (Bit0 = Video On, Bit4 = Key On, Bit8 = Format, 0=CCIR, 1=YUV2)", NULL, NULL },   
    { NV_REG_LIST_END, NULL, NULL, NULL}, // sentinel value 
};
//#endif