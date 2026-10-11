/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PGRAPH (Scene Graph for 2D/3D Accelerated Graphics)
 *
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
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>
#include <86box/nv/classes/vid_nv3_classes.h>

extern uint32_t nv3_debug_class_count[32];

// Initialise the PGRAPH subsystem.
void nv3_pgraph_init(void)
{
    nv_log("Initialising PGRAPH...");
    // Set up the vblank interrupt
    nv3->nvbase.svga.vblank_start = nv3_pgraph_vblank_start;
    nv_log("Done!\n");    
}

uint32_t nv3_pgraph_read(uint32_t address) 
{ 
    // before doing anything, check that this is even enabled..

    if (!((nv3->pmc.enable >> NV3_PMC_ENABLE_PGRAPH) & NV3_PMC_ENABLE_PGRAPH_ENABLED))
    {
        nv_log("Repressing PGRAPH read. The subsystem is disabled according to pmc_enable, returning 0\n");
        return 0x00;
    }

    uint32_t ret = 0x00;

    // todo: friendly logging
    
    switch (address)
    {
        case NV3_PGRAPH_DEBUG_0:
            ret = nv3->pgraph.debug_0;
            break;
        case NV3_PGRAPH_DEBUG_1:
            ret = nv3->pgraph.debug_1;
            break;
        case NV3_PGRAPH_DEBUG_2:
            ret = nv3->pgraph.debug_2;
            break;
        case NV3_PGRAPH_DEBUG_3:
            ret = nv3->pgraph.debug_3;
            break;
        //interrupt status and enable regs
        case NV3_PGRAPH_INTR_0:
            ret = nv3->pgraph.intr_0;
            break;
        case NV3_PGRAPH_INTR_1:
            ret = nv3->pgraph.intr_1;
            break;
        case NV3_PGRAPH_DMA_INTR_0:
            ret = nv3->pgraph.intr_dma;
            break;
        case NV3_PGRAPH_DMA_INTR_EN_0:
            ret = nv3->pgraph.intr_en_dma;
            break;
        case NV3_PGRAPH_INTR_EN_0:
            ret = nv3->pgraph.intr_en_0;
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PGRAPH_INTR_EN_1:
            ret = nv3->pgraph.intr_en_1;
            nv3_pmc_handle_interrupts(true);
            break;
        // Some of this is a temporary implementation so that we can just debug what the current state looks like during the driver initialisation process            
        // In the future, these will most likely have their own functions...
        // Context Switching (THIS IS CONTROLLED BY PFIFO!)
        case NV3_PGRAPH_CTX_SWITCH:
            ret = nv3->pgraph.context_switch;
            break;
        case NV3_PGRAPH_CONTEXT_CONTROL:
            ret = *(uint32_t*)&nv3->pgraph.context_control;
            break;
        case NV3_PGRAPH_CONTEXT_USER:
            ret = nv3->pgraph.context_user;
            break;
        // Clip
        case NV3_PGRAPH_ABS_UCLIP_XMIN:
            ret = nv3->pgraph.uclip_min[0] & 0x3FFFF;
            break;
        case NV3_PGRAPH_ABS_UCLIP_YMIN:
            ret = nv3->pgraph.uclip_min[1] & 0x3FFFF;
            break;
        case NV3_PGRAPH_ABS_UCLIP_XMAX:
            ret = nv3->pgraph.uclip_max[0] & 0x3FFFF;
            break;
        case NV3_PGRAPH_ABS_UCLIP_YMAX:
            ret = nv3->pgraph.uclip_max[1] & 0x3FFFF;
            break;
        // Canvas
        case NV3_PGRAPH_SRC_CANVAS_MIN:
            ret = nv3->pgraph.src_canvas_min;
            break;
        case NV3_PGRAPH_SRC_CANVAS_MAX:
            ret = nv3->pgraph.src_canvas_max;
            break;
        case NV3_PGRAPH_DST_CANVAS_MIN:
            ret = nv3->pgraph.dst_canvas_min;
            break;
        case NV3_PGRAPH_DST_CANVAS_MAX:
            ret = nv3->pgraph.dst_canvas_max;
            break;
        // Pattern
        case NV3_PGRAPH_PATTERN_COLOR_0_RGB:
            ret = nv3->pgraph.pattern_mono_rgb[0];
            break;
        case NV3_PGRAPH_PATTERN_COLOR_0_ALPHA:
            ret = nv3->pgraph.pattern_mono_a[0];
            break;
        case NV3_PGRAPH_PATTERN_COLOR_1_RGB:
            ret = nv3->pgraph.pattern_mono_rgb[1];
            break;
        case NV3_PGRAPH_PATTERN_COLOR_1_ALPHA:
            ret = nv3->pgraph.pattern_mono_a[1];
            break;
        case NV3_PGRAPH_PATTERN_BITMAP_0:
            ret = nv3->pgraph.pattern_mono_bitmap[0];
            break;
        case NV3_PGRAPH_PATTERN_BITMAP_1:
            ret = nv3->pgraph.pattern_mono_bitmap[1];
            break;
        case NV3_PGRAPH_PATTERN_SHAPE:
            ret = nv3->pgraph.pattern_shape;
            break;
        // Beta factor
        case NV3_PGRAPH_BETA:
            ret = nv3->pgraph.beta_factor;
            break;
        case NV3_PGRAPH_ROP3:
            ret = nv3->pgraph.rop;
            break;
        case NV3_PGRAPH_CHROMA_KEY:
            ret = nv3->pgraph.chroma_key;
            break;
        case NV3_PGRAPH_PLANE_MASK:
            ret = nv3->pgraph.plane_mask;
            break;
        // Surfaces
        case NV3_PGRAPH_SURF_OFFSET(0) ... NV3_PGRAPH_SURF_OFFSET(3):
            ret = nv3->pgraph.boffset[(address - NV3_PGRAPH_SURF_OFFSET(0)) >> 2];
            break;
        case NV3_PGRAPH_SURF_PITCH(0) ... NV3_PGRAPH_SURF_PITCH(3):
            ret = nv3->pgraph.bpitch[(address - NV3_PGRAPH_SURF_PITCH(0)) >> 2];
            break;
        case NV3_PGRAPH_SURF_FORMAT:
            for (int i = 0; i < NV3_PGRAPH_MAX_BUFFERS; i++)
                ret |= (nv3->pgraph.bpixel[i] & 0xF) << (i * 4);
            break;
        case NV3_PGRAPH_D3D_CONFIG:
            ret = nv3->pgraph.d3d_config;
            break;
        case NV3_PGRAPH_ALPHACNTRL:
            ret = nv3->pgraph.d3d_alpha;
            break;
        // DMA
        case NV3_PGRAPH_DMA:
            ret = nv3->pgraph.dma_settings;
            break;
        case NV3_PGRAPH_NOTIFY:
            ret = nv3->pgraph.notifier;
            break;
        // Clip rectangles
        case NV3_PGRAPH_CLIP0_MIN:
            ret = nv3->pgraph.cliprect_min[0];
            break;
        case NV3_PGRAPH_CLIP0_MAX:
            ret = nv3->pgraph.cliprect_max[0];
            break;
        case NV3_PGRAPH_CLIP1_MIN:
            ret = nv3->pgraph.cliprect_min[1];
            break;
        case NV3_PGRAPH_CLIP1_MAX:
            ret = nv3->pgraph.cliprect_max[1];
            break;
        case NV3_PGRAPH_CLIP_MISC:
            ret = nv3->pgraph.cliprect_ctrl;
            break;
        case NV3_PGRAPH_FIFO_ACCESS:
            ret = nv3->pgraph.fifo_access;
            break;
        // Overall Status
        case NV3_PGRAPH_STATUS:
            ret = *(uint32_t*)&nv3->pgraph.status;
            break;
        // Trapped Address
        case NV3_PGRAPH_TRAPPED_ADDRESS:
            ret = nv3->pgraph.trapped_address;
            break;
        case NV3_PGRAPH_TRAPPED_DATA:
            ret = nv3->pgraph.trapped_data;
            break;
        case NV3_PGRAPH_INSTANCE:
            ret = nv3->pgraph.instance;
            break;
        case NV3_PGRAPH_CTX_SWITCH_C:
            ret = nv3->pgraph.ctx_switch_c;
            break;
        case NV3_PGRAPH_TRAPPED_INSTANCE:
            ret = nv3->pgraph.trapped_instance;
            break;
        /* The rest -- among them what the resman's channel switch saves and restores:
           SOURCE_COLOR, MONO_COLOR0, MISC24_0/1, PASSTHRU, the
           D3D_* vertex registers, XY_LOGIC_MISC*, X/Y_MISC, ABS_ICLIP, CLIPX/Y, ABS_UCLIPA, the
           ABS_X/Y and ZFOG RAMs, EXCEPTIONS -- read back what was written */
        default:
            if (address <= NV3_PGRAPH_REGISTER_END)
                ret = nv3->pgraph.regs[(address & 0x1FFF) >> 2];
            break;
    }

    /* Special exception for memory areas */
    if (address >= NV3_PGRAPH_CONTEXT_CACHE(0)
    && address < NV3_PGRAPH_CONTEXT_CACHE(NV3_PGRAPH_CONTEXT_CACHE_SIZE))
    {
        // Addresses should be aligned to 4 bytes.
        uint32_t entry = (address - NV3_PGRAPH_CONTEXT_CACHE(0)) >> 2;
        ret = nv3->pgraph.context_cache[entry];
        nv_log_verbose_only("PGRAPH Context Cache Read (Entry=%04x Value=%04x)\n", entry, nv3->pgraph.context_cache[entry]);
    }

    return ret; 
}

void nv3_pgraph_write(uint32_t address, uint32_t value) 
{
    if (!((nv3->pmc.enable >> NV3_PMC_ENABLE_PGRAPH) & NV3_PMC_ENABLE_PGRAPH_ENABLED))
    {
        nv_log("Repressing PGRAPH write. The subsystem is disabled according to pmc_enable\n");
        return;
    }

    switch (address)
    {
        case NV3_PGRAPH_DEBUG_0:
            nv3->pgraph.debug_0 = value;
            break;
        case NV3_PGRAPH_DEBUG_1:
            nv3->pgraph.debug_1 = value;
            break;
        case NV3_PGRAPH_DEBUG_2:
            nv3->pgraph.debug_2 = value;
            break;
        case NV3_PGRAPH_DEBUG_3:
            nv3->pgraph.debug_3 = value;
            break;
        //interrupt status and enable regs
        case NV3_PGRAPH_INTR_0:
            nv3->pgraph.intr_0 &= ~value;
            //we changed interrupt state
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PGRAPH_INTR_1:
            nv3->pgraph.intr_1 &= ~value;
            //we changed interrupt state
            nv3_pmc_handle_interrupts(true);
            break;
        // Only bits divisible by 4 matter
        // and only bit0-16 is defined in intr_1 
        case NV3_PGRAPH_INTR_EN_0:
            nv3->pgraph.intr_en_0 = value & 0x11111111;                     
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PGRAPH_INTR_EN_1:
            nv3->pgraph.intr_en_1 = value & 0x00011111; 
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PGRAPH_DMA_INTR_0:
            nv3->pgraph.intr_dma &= ~value;
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PGRAPH_DMA_INTR_EN_0:
            nv3->pgraph.intr_en_dma = value & 0x00011111;
            nv3_pmc_handle_interrupts(true);
            break;
        // A lot of this is currently a temporary implementation so that we can just debug what the current state looks like
        // during the driver initialisation process            

        // In the future, these will most likely have their own functions...

        // Context Swithcing (THIS IS CONTROLLED BY PFIFO!)
        case NV3_PGRAPH_CTX_SWITCH:
            nv3->pgraph.context_switch = value;
            break;
        case NV3_PGRAPH_CONTEXT_CONTROL:
            *(uint32_t*)&nv3->pgraph.context_control = value;
            break;
        case NV3_PGRAPH_CONTEXT_USER:
        {
            /* the resman loading another channel's context: its primitive state comes with it
               (on the chip the resman saves and reloads those registers itself) */
            uint32_t old_chid = (nv3->pgraph.context_user >> NV3_PGRAPH_CONTEXT_USER_CHANNEL) & 0x7F;
            uint32_t new_chid = (value >> NV3_PGRAPH_CONTEXT_USER_CHANNEL) & 0x7F;

            if (old_chid != new_chid) {
                nv3->prim_saved[old_chid]          = nv3->pgraph.prim;
                nv3->pgraph.prim                   = nv3->prim_saved[new_chid];
                nv3->pgraph.prim.d3d.tex_mapped    = false; /* page lookups go through the channel's DMA objects */
            }
            nv3->pgraph.context_user = value;
            break;
        }
        // Clip (18-bit signed)
        case NV3_PGRAPH_ABS_UCLIP_XMIN:
            nv3->pgraph.uclip_min[0] = ((int32_t) (value << 14)) >> 14;
            break;
        case NV3_PGRAPH_ABS_UCLIP_YMIN:
            nv3->pgraph.uclip_min[1] = ((int32_t) (value << 14)) >> 14;
            break;
        case NV3_PGRAPH_ABS_UCLIP_XMAX:
            nv3->pgraph.uclip_max[0] = ((int32_t) (value << 14)) >> 14;
            break;
        case NV3_PGRAPH_ABS_UCLIP_YMAX:
            nv3->pgraph.uclip_max[1] = ((int32_t) (value << 14)) >> 14;
            break;
        // Canvas
        case NV3_PGRAPH_SRC_CANVAS_MIN:
            nv3->pgraph.src_canvas_min = value & 0x3FFF07FF;
            break;
        case NV3_PGRAPH_SRC_CANVAS_MAX:
            nv3->pgraph.src_canvas_max = value & 0x3FFF07FF;
            break;
        case NV3_PGRAPH_DST_CANVAS_MIN:
            nv3->pgraph.dst_canvas_min = value & 0x3FFF07FF;
            break;
        case NV3_PGRAPH_DST_CANVAS_MAX:
            nv3->pgraph.dst_canvas_max = value & 0x3FFF07FF;
            break;
        // Pattern
        case NV3_PGRAPH_PATTERN_COLOR_0_RGB:
            nv3->pgraph.pattern_mono_rgb[0] = value & 0x3FFFFFFF;
            break;
        case NV3_PGRAPH_PATTERN_COLOR_0_ALPHA:
            nv3->pgraph.pattern_mono_a[0] = value & 0xFF;
            break;
        case NV3_PGRAPH_PATTERN_COLOR_1_RGB:
            nv3->pgraph.pattern_mono_rgb[1] = value & 0x3FFFFFFF;
            break;
        case NV3_PGRAPH_PATTERN_COLOR_1_ALPHA:
            nv3->pgraph.pattern_mono_a[1] = value & 0xFF;
            break;
        case NV3_PGRAPH_PATTERN_BITMAP_0:
            nv3->pgraph.pattern_mono_bitmap[0] = value;
            break;
        case NV3_PGRAPH_PATTERN_BITMAP_1:
            nv3->pgraph.pattern_mono_bitmap[1] = value;
            break;
        case NV3_PGRAPH_PATTERN_SHAPE:
            nv3->pgraph.pattern_shape = value & 0x03;
            break;
        // Beta factor
        case NV3_PGRAPH_BETA:
            nv3->pgraph.beta_factor = value & 0x7F800000;
            break;
        case NV3_PGRAPH_ROP3:
            nv3->pgraph.rop = value & 0xFF;
            break;
        case NV3_PGRAPH_CHROMA_KEY:
            nv3->pgraph.chroma_key = value & 0x7FFFFFFF;
            break;
        case NV3_PGRAPH_PLANE_MASK:
            nv3->pgraph.plane_mask = value;
            break;
        // Surfaces
        case NV3_PGRAPH_SURF_OFFSET(0) ... NV3_PGRAPH_SURF_OFFSET(3):
            nv3->pgraph.boffset[(address - NV3_PGRAPH_SURF_OFFSET(0)) >> 2] = value & 0x7FFFF0;
            break;
        case NV3_PGRAPH_SURF_PITCH(0) ... NV3_PGRAPH_SURF_PITCH(3):
            nv3->pgraph.bpitch[(address - NV3_PGRAPH_SURF_PITCH(0)) >> 2] = value & 0x1FF0;
            break;
        case NV3_PGRAPH_SURF_FORMAT:
            for (int i = 0; i < NV3_PGRAPH_MAX_BUFFERS; i++)
                nv3->pgraph.bpixel[i] = (value >> (i * 4)) & 0x7;
            break;
        case NV3_PGRAPH_D3D_CONFIG:
            nv3->pgraph.d3d_config = value;
            break;
        case NV3_PGRAPH_ALPHACNTRL:
            nv3->pgraph.d3d_alpha = value & 0xFFF;
            break;
        // DMA
        case NV3_PGRAPH_DMA:
            nv3->pgraph.dma_settings = value & 0xFFFF;
            break;
        case NV3_PGRAPH_NOTIFY:
            /* instance (15:0), armed (16), select (23:20) */
            nv3->pgraph.notifier       = value & 0x00F1FFFF;
            nv3->pgraph.notify_pending = (value >> NV3_PGRAPH_NOTIFY_REQUEST_PENDING) & 1;
            break;
        // Clip rectangles
        case NV3_PGRAPH_CLIP0_MIN:
            nv3->pgraph.cliprect_min[0] = value;
            break;
        case NV3_PGRAPH_CLIP0_MAX:
            nv3->pgraph.cliprect_max[0] = value;
            break;
        case NV3_PGRAPH_CLIP1_MIN:
            nv3->pgraph.cliprect_min[1] = value;
            break;
        case NV3_PGRAPH_CLIP1_MAX:
            nv3->pgraph.cliprect_max[1] = value;
            break;
        case NV3_PGRAPH_CLIP_MISC:
            nv3->pgraph.cliprect_ctrl = value & 0x113;
            break;
        case NV3_PGRAPH_FIFO_ACCESS:
            nv3->pgraph.fifo_access = value & 1;
            /* methods queued while PGRAPH was stopped go through now, and a waiting pusher resumes */
            if (nv3->pgraph.fifo_access) {
                nv3_pfifo_cache1_drain();
                nv3_pfifo_dma_pusher_kick();
            }
            break;
        // Overall Status
        case NV3_PGRAPH_STATUS:
            *(uint32_t*)&nv3->pgraph.status = value;
            break;
        // Trapped Address
        case NV3_PGRAPH_TRAPPED_ADDRESS:
            nv3->pgraph.trapped_address = value;
            break;
        case NV3_PGRAPH_TRAPPED_DATA:
            nv3->pgraph.trapped_data = value;
            break;
        case NV3_PGRAPH_INSTANCE:
            nv3->pgraph.instance = value & 0xFFFF;
            break;
        case NV3_PGRAPH_CTX_SWITCH_C:
            nv3->pgraph.ctx_switch_c = value & 0x1FFFF;
            break;
        case NV3_PGRAPH_TRAPPED_INSTANCE:
            nv3->pgraph.trapped_instance = value;
            break;
        default:
            if (address <= NV3_PGRAPH_REGISTER_END)
                nv3->pgraph.regs[(address & 0x1FFF) >> 2] = value;
            break;
    }

    /* Special exception for memory areas */
    if (address >= NV3_PGRAPH_CONTEXT_CACHE(0)
    && address < NV3_PGRAPH_CONTEXT_CACHE(NV3_PGRAPH_CONTEXT_CACHE_SIZE))
    {
        // Addresses should be aligned to 4 bytes.
        uint32_t entry = (address - NV3_PGRAPH_CONTEXT_CACHE(0)) >> 2;

        nv_log_verbose_only("PGRAPH Context Cache Write (Entry=%04x Value=0x%08x)\n", entry, value);
        nv3->pgraph.context_cache[entry] = value & 0x3FF3F71F;
        {
            extern uint32_t nv3_swm_trace_left;
            if (nv3_swm_trace_left)
                always_log("nv3: ctx_cache[%d] = %08x\n", entry, value);
        }
    }
}

// Fire a VALID Pgraph interrupt: num is the bit# of the interrupt in the GPU subsystem INTR_EN register.
void nv3_pgraph_interrupt_valid(uint32_t num)
{
    nv3->pgraph.intr_0 |= (1 << num);
    nv3_pmc_handle_interrupts(true);
}

// Fire an INVALID pgraph interrupt
// The method in TRAPPED_ADDR/DATA is not one the hardware executes: flag it in
// INVALID and INTR bit 0 and stop taking methods until the driver's interrupt
// handler (which runs software methods such as the patchcord ones) turns
// FIFO access back on. An armed notify also raises NOTIFY.
/* The last methods submitted, logged with the first few traps */
#define NV3_METHOD_TRACE 256
static struct {
    uint32_t addr, data;
} nv3_method_trace[NV3_METHOD_TRACE];
static uint32_t nv3_method_trace_pos;
static uint32_t nv3_method_trace_dumps;

static void
nv3_pgraph_trace_method(uint32_t addr, uint32_t data)
{
    uint32_t i = nv3_method_trace_pos++ % NV3_METHOD_TRACE;

    nv3_method_trace[i].addr = addr;
    nv3_method_trace[i].data = data;
}

/* All of the trace, oldest first, for the debug dump */
void
nv3_pgraph_dump_methods(void)
{
    for (uint32_t n = 0; n < NV3_METHOD_TRACE; n++) {
        uint32_t i = (nv3_method_trace_pos + n) % NV3_METHOD_TRACE;
        if (nv3_method_trace[i].addr || nv3_method_trace[i].data)
            always_log("nv3:   m %08x %08x" "%c", nv3_method_trace[i].addr, nv3_method_trace[i].data, 10);
    }
}

/* A method for a channel PGRAPH has not loaded: CONTEXT_SWITCH (INTR_0 bit 4), FIFO access off */
void nv3_pgraph_interrupt_context_switch(void)
{
    nv3->pgraph.intr_0 |= (1 << NV3_PGRAPH_INTR_0_CONTEXT_SWITCH);
    nv3->pgraph.fifo_access = false;
    nv3_pmc_handle_interrupts(true);
}

void nv3_pgraph_interrupt_invalid(uint32_t num)
{
    if (nv3_method_trace_dumps < 12) {
        nv3_method_trace_dumps++;
        always_log("nv3: trap %d at %08x %08x; methods before it (chid<<24|class<<16|subch<<13|mthd, data):\n", num,
                   nv3->pgraph.trapped_address, nv3->pgraph.trapped_data);
        for (uint32_t n = NV3_METHOD_TRACE - 24; n < NV3_METHOD_TRACE; n++) {
            uint32_t i = (nv3_method_trace_pos + n) % NV3_METHOD_TRACE;
            if (nv3_method_trace[i].addr || nv3_method_trace[i].data)
                always_log("nv3:   %08x %08x\n", nv3_method_trace[i].addr, nv3_method_trace[i].data);
        }
    }
    /* Debug: while M2MF tracing is on, a trapped M2MF method also logs what the driver's handler does */
    {
        extern uint32_t nv3_m2mf_trace_left, nv3_mmio_trace_left;
        if (nv3_m2mf_trace_left && ((nv3->pgraph.trapped_address >> 16) & 0x1F) == 0x0D) {
            always_log("nv3: m2mf trap %08x %08x\n", nv3->pgraph.trapped_address, nv3->pgraph.trapped_data);
            nv3_mmio_trace_left = 60;
        }
    }

    nv3->pgraph.intr_1 |= (1 << num);
    nv3->pgraph.intr_0 |= 1;
    if (nv3->pgraph.notify_pending && ((nv3->pgraph.notifier >> NV3_PGRAPH_NOTIFY_REQUEST_TYPE) & 0xF))
        nv3->pgraph.intr_0 |= (1 << NV3_PGRAPH_INTR_0_SOFTWARE_NOTIFY);
    nv3->pgraph.fifo_access = false;
    nv3_pmc_handle_interrupts(true);
}

// VBlank. Fired every single frame.
void nv3_pgraph_vblank_start(UNUSED(svga_t* svga))
{
    nv3_pgraph_interrupt_valid(NV3_PGRAPH_INTR_0_VBLANK);
    nv3_pvideo_vblank();
}

/* Arbitrates graphics object submission to the right object types */
void nv3_pgraph_submit(uint32_t param, uint16_t method, uint8_t channel, uint8_t subchannel, uint8_t class_id, nv3_ramin_context_t context)
{
    /* What the driver's interrupt handler reads if this method traps */
    nv3->pgraph.trapped_address = (method & 0x1FFC) | ((uint32_t) (subchannel & 7) << 13)
        | ((uint32_t) (class_id & 0x1F) << 16) | ((uint32_t) (channel & 0x7F) << 24);
    nv3->pgraph.trapped_data     = param;
    nv3->pgraph.trapped_instance = context.ramin_offset;

    // extract the channel id so we can see if we need to context switch

    //uint8_t old_channel_id = (nv3->pgraph.context_user >> NV3_PGRAPH_CONTEXT_USER_CHANNEL) & 0x7F;

    //if (old_channel_id != channel)
    //{
    //    nv3_pgraph_interrupt_valid(NV3_PGRAPH_INTR_0_CONTEXT_SWITCH);
    //    return; 
    //}

    // set ctx_user for the drivers
    /* the channel is CTX_USER's already (the puller switches contexts first) */
    uint32_t old_subchannel = (nv3->pgraph.context_user >> NV3_PGRAPH_CONTEXT_USER_SUBCHANNEL) & 7;
    nv3->pgraph.context_user = (nv3->pgraph.context_user & 0xFF000000) | (context.context & 0x1F0000)
    | ((uint32_t)subchannel << NV3_PGRAPH_CONTEXT_USER_SUBCHANNEL);

    // class id can be derived from the context but we debug log it before we get here
    // Obtain the grobj information from the context in ramin

    nv3_grobj_t grobj = {0};

    // we need to shift left by 4 to get the real address, something to do with the 16 byte unit of reversal 
    uint32_t real_ramin_base = context.ramin_offset << 4;

    // readin our grobj
    grobj.grobj_0 = nv3_ramin_read32(real_ramin_base, nv3);
    grobj.grobj_1 = nv3_ramin_read32(real_ramin_base + 4, nv3);
    grobj.grobj_2 = nv3_ramin_read32(real_ramin_base + 8, nv3);
    grobj.grobj_3 = nv3_ramin_read32(real_ramin_base + 12, nv3);

    /* The object's options (colour format, operation, buffers...) live in PGRAPH: a bind loads the
       grobj's word 0 into the subchannel's CTX_CACHE entry, and a change of subchannel (or a bind)
       loads CTX_SWITCH from it when DEBUG_1 bit 20 allows (envytools hwtest nv03_pgraph_mthd).
       Drawing uses CTX_SWITCH, which the driver's software methods and context switches edit. */
    if (!method) {
        extern uint32_t nv3_swm_trace_left;
        nv3->pgraph.context_cache[subchannel & 7] = grobj.grobj_0 & 0x3FF3F71F;
        if (nv3_swm_trace_left)
            always_log("nv3: bind subch %d handle %08x ctx %08x grobj0 %08x\n", subchannel, param, context.context, grobj.grobj_0);
    }
    if ((old_subchannel != (subchannel & 7)) || !method) {
        if ((nv3->pgraph.debug_1 >> NV3_PGRAPH_DEBUG_1_CONTEXT) & 1)
            nv3->pgraph.context_switch = nv3->pgraph.context_cache[subchannel & 7];
    }
    grobj.grobj_0 = nv3->pgraph.context_switch;

    /* The DMA pointers (CTX_SWITCH_B/C, NOTIFY's instance) are PGRAPH registers. With DEBUG_1
       bit 16 set they are reloaded from the grobj (words 1 and 2) when a DMA-using object
       (M2MF, scaled image from memory, image to memory, D3D) or a NOTIFY method arrives for
       another instance than CTX_SWITCH_I; otherwise they keep what the driver wrote
       (envytools hwtest nv03_pgraph_mthd). */
    if (((nv3->pgraph.debug_1 >> NV3_PGRAPH_DEBUG_1_INSTANCE) & 1)
        && ((context.ramin_offset & 0xFFFF) != nv3->pgraph.instance)
        && ((class_id == nv3_pgraph_class0d_m2mf) || (class_id == nv3_pgraph_class0e_scaled_image_from_memory)
            || (class_id == nv3_pgraph_class14_transfer2memory) || (class_id == nv3_pgraph_class17_d3d5tri_zeta_buffer)
            || (method == NV3_SET_NOTIFY))) {
        nv3->pgraph.instance     = context.ramin_offset & 0xFFFF;
        nv3->pgraph.dma_settings = grobj.grobj_1 & 0xFFFF;
        nv3->pgraph.notifier     = (nv3->pgraph.notifier & 0xF10000) | (grobj.grobj_1 >> 16);
        nv3->pgraph.ctx_switch_c = grobj.grobj_2 & 0x1FFFF;
    }

    nv_log_verbose_only("**** About to execute method **** method=0x%04x param=0x%08x, channel=%d.%d, class=%s, grobj=0x%08x 0x%08x 0x%08x 0x%08x\n",
        method, param, channel, subchannel, nv3_class_names[class_id], grobj.grobj_0, grobj.grobj_1, grobj.grobj_2, grobj.grobj_3);

    nv3_debug_class_count[class_id & 0x1F]++;
    nv3_pgraph_trace_method(nv3->pgraph.trapped_address, param);

    /* Methods below 0x104 are shared across all classids, so call generic_method for that*/
    if (method <= NV3_SET_NOTIFY)
        nv3_generic_method(param, method, context, grobj);
    else
    {
        // By this point, we already ANDed the class ID to 0x1F.
        // Send the grobj, the context, the method and the name off to actually be acted upon.
        switch (class_id)
        {
            case nv3_pgraph_class01_beta_factor:
                nv3_class_001_method(param, method, context, grobj);
                break; 
            case nv3_pgraph_class02_rop:
                nv3_class_002_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class03_chroma_key:
                nv3_class_003_method(param, method, context, grobj);
                break; 
            case nv3_pgraph_class04_plane_mask:
                nv3_class_004_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class05_clipping_rectangle:
                nv3_class_005_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class06_pattern:
                nv3_class_006_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class07_rectangle:
                nv3_class_007_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class08_point:
                nv3_class_008_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class09_line:
                nv3_class_009_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0a_lin:
                nv3_class_00a_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0b_triangle:
                nv3_class_00b_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0c_w95txt:
                nv3_class_00c_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0d_m2mf:
                nv3_class_00d_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0e_scaled_image_from_memory:
                nv3_class_00e_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class10_blit:
                nv3_class_010_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class11_image:
                nv3_class_011_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class12_bitmap:
                nv3_class_012_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class14_transfer2memory:
                nv3_class_014_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class15_stretched_image_from_cpu:
                nv3_class_015_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class17_d3d5tri_zeta_buffer:
                nv3_class_017_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class18_point_zeta_buffer:
                nv3_class_018_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class1c_image_in_memory:
                nv3_class_01c_method(param, method, context, grobj);
                break;             
            default:
                /* a class with no hardware here: hand the method to the driver as a software method,
                   as the class handlers do with methods they don't know (this used to be fatal()) */
                nv_warning("NV3: method %04x on unimplemented class %02x (%s)\n", method, class_id, nv3_class_names[class_id & 0x1F]);
                nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
                return;
        }
    }

    nv3_notify_if_needed(param, method, context, grobj);
}