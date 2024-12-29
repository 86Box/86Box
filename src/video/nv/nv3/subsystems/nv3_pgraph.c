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
 *
 *          Copyright 2024-2025 starfrost
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86Box/86box.h>
#include <86Box/device.h>
#include <86Box/mem.h>
#include <86box/pci.h>
#include <86Box/rom.h> // DEPENDENT!!!
#include <86Box/video.h>
#include <86Box/nv/vid_nv.h>
#include <86Box/nv/vid_nv3.h>


// Initialise the PGRAPH subsystem.
void nv3_pgraph_init()
{
    nv_log("NV3: Initialising PGRAPH...");
    // Set up the vblank interrupt
    nv3->nvbase.svga.vblank_start = nv3_pgraph_vblank_start;
    nv_log("Done!\n");    
}

//
// ****** PGRAPH register list START ******
//

nv_register_t pgraph_registers[] = {
    { NV3_PGRAPH_DEBUG_0, "PGRAPH Debug 0", NULL, NULL },
    { NV3_PGRAPH_DEBUG_1, "PGRAPH Debug 1", NULL, NULL },
    { NV3_PGRAPH_DEBUG_2, "PGRAPH Debug 2", NULL, NULL },
    { NV3_PGRAPH_DEBUG_3, "PGRAPH Debug 3", NULL, NULL },
    { NV3_PGRAPH_INTR_0, "PGRAPH Interrupt Status 0", NULL, NULL },
    { NV3_PGRAPH_INTR_EN_0, "PGRAPH Interrupt Enable 0", NULL, NULL },
    { NV3_PGRAPH_INTR_1, "PGRAPH Interrupt Status 1", NULL, NULL },
    { NV3_PGRAPH_INTR_EN_1, "PGRAPH Interrupt Enable 1", NULL, NULL },
    { NV3_PGRAPH_CONTEXT_SWITCH, "PGRAPH DMA Context Switch", NULL, NULL },
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
    { NV3_PGRAPH_PATTERN_COLOR_0_0, "PGRAPH Pattern Color 0_0 (Bits 29:20 = Red, Bits 19:10 = Green, Bits 9:0 = Blue)", NULL, NULL, },
    { NV3_PGRAPH_PATTERN_COLOR_0_1, "PGRAPH Pattern Color 0_1 (Bits 7:0 = Alpha)", NULL, NULL, },
    { NV3_PGRAPH_PATTERN_COLOR_1_0, "PGRAPH Pattern Color 1_0 (Bits 29:20 = Red, Bits 19:10 = Green, Bits 9:0 = Blue)", NULL, NULL, },
    { NV3_PGRAPH_PATTERN_COLOR_1_1, "PGRAPH Pattern Color 1_1 (Bits 7:0 = Alpha)", NULL, NULL, },
    { NV3_PGRAPH_PATTERN_BITMAP_HIGH, "PGRAPH Pattern Bitmap (High 32bits)", NULL, NULL},
    { NV3_PGRAPH_PATTERN_BITMAP_LOW, "PGRAPH Pattern Bitmap (Low 32bits)", NULL, NULL},
    { NV3_PGRAPH_PATTERN_SHAPE, "PGRAPH Pattern Shape (1:0 - 0=8x8, 1=64x1, 2=1x64)", NULL, NULL},
    { NV3_PGRAPH_ROP3, "PGRAPH Render Operation ROP3 (2^3 bits = 256 possible operations)", NULL, NULL},
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
    { NV3_PGRAPH_TRAPPED_INSTANCE, "PGRAPH Trapped Object Instance", NULL, NULL },

    { NV_REG_LIST_END, NULL, NULL, NULL}, // sentinel value 
};

uint32_t nv3_pgraph_read(uint32_t address) 
{ 
    // before doing anything, check that this is even enabled..

    if (!(nv3->pmc.enable >> NV3_PMC_ENABLE_PGRAPH)
    & NV3_PMC_ENABLE_PGRAPH_ENABLED)
    {
        nv_log("NV3: Repressing PGRAPH read. The subsystem is disabled according to pmc_enable, returning 0\n");
        return 0x00;
    }

    uint32_t ret = 0x00;

    nv_register_t* reg = nv_get_register(address, pgraph_registers, sizeof(pgraph_registers)/sizeof(pgraph_registers[0]));

    // todo: friendly logging
    
    nv_log("NV3: PGRAPH Read from 0x%08x", address);

    // if the register actually exists
    if (reg)
    {
        // on-read function
        if (reg->on_read)
            ret = reg->on_read();
        else
        {
            switch (reg->address)
            {
                //interrupt status and enable regs
                case NV3_PGRAPH_INTR_0:
                    ret = nv3->pgraph.interrupt_status_0;
                    break;
                case NV3_PGRAPH_INTR_1:
                    ret = nv3->pgraph.interrupt_status_1;
                    break;
                case NV3_PGRAPH_INTR_EN_0:
                    ret = nv3->pgraph.interrupt_enable_0;
                    break;
                case NV3_PGRAPH_INTR_EN_1:
                    ret = nv3->pgraph.interrupt_enable_1;
                    break;
            }
        }

        if (reg->friendly_name)
            nv_log(": %s (value = 0x%08x)\n", reg->friendly_name, ret);
        else   
            nv_log("\n");
    }
    else
    {
        /* Special exception for memory areas */
        if (address >= NV3_PGRAPH_CONTEXT_CACHE(0)
        && address <= NV3_PGRAPH_CONTEXT_CACHE(NV3_PGRAPH_CONTEXT_CACHE_SIZE))
        {
            // Addresses should be aligned to 4 bytes.
            uint32_t entry = (address - NV3_PGRAPH_CONTEXT_CACHE(0));

            nv_log("NV3: PGRAPH Context Cache Read (Entry=%04x Value=%04x)\n", entry, nv3->pgraph.context_cache[entry]);
        }
        else /* Completely unknown */
        {
            nv_log(": Unknown register read (address=0x%08x), returning 0x00\n", address);
        }
    }

    return ret; 
}

void nv3_pgraph_write(uint32_t address, uint32_t value) 
{
    if (!(nv3->pmc.enable >> NV3_PMC_ENABLE_PGRAPH)
    & NV3_PMC_ENABLE_PGRAPH_ENABLED)
    {
        nv_log("NV3: Repressing PGRAPH write. The subsystem is disabled according to pmc_enable\n");
        return;
    }

    nv_register_t* reg = nv_get_register(address, pgraph_registers, sizeof(pgraph_registers)/sizeof(pgraph_registers[0]));

    nv_log("NV3: PGRAPH Write 0x%08x -> 0x%08x\n", value, address);

    // if the register actually exists
    if (reg)
    {
        if (reg->friendly_name)
            nv_log(": %s\n", reg->friendly_name);
        else   
            nv_log("\n");

        // on-read function
        if (reg->on_write)
            reg->on_write(value);
        else
        {
            switch (reg->address)
            {
                //interrupt status and enable regs
                case NV3_PGRAPH_INTR_0:
                    nv3->pgraph.interrupt_status_0 &= ~value;
                    //we changed interrupt state
                    nv3_pmc_clear_interrupts();
                    break;
                case NV3_PGRAPH_INTR_1:
                    nv3->pgraph.interrupt_status_1 &= ~value;
                    //we changed interrupt state
                    nv3_pmc_clear_interrupts();
                    break;
                // Only bits divisible by 4 matter
                // and only bit0-16 is defined in intr_1 
                case NV3_PGRAPH_INTR_EN_0:
                    nv3->pgraph.interrupt_enable_0 = value & 0x11111111; 
                    nv3_pmc_handle_interrupts(true);
                    break;
                case NV3_PGRAPH_INTR_EN_1:
                    nv3->pgraph.interrupt_enable_1 = value & 0x00011111; 
                    nv3_pmc_handle_interrupts(true);

                    break;
            }
        }
    }
    else
    {
        /* Special exception for memory areas */
        if (address >= NV3_PGRAPH_CONTEXT_CACHE(0)
        && address <= NV3_PGRAPH_CONTEXT_CACHE(NV3_PGRAPH_CONTEXT_CACHE_SIZE))
        {
            // Addresses should be aligned to 4 bytes.
            uint32_t entry = (address - NV3_PGRAPH_CONTEXT_CACHE(0));

            nv_log("NV3: PGRAPH Context Cache Write (Entry=%04x Value=%04x)\n", entry, value);
            nv3->pgraph.context_cache[entry] = value;
        }
    }
}

// Fire a VALID Pgraph interrupt: num is the bit# of the interrupt in the GPU subsystem INTR_EN register.
void nv3_pgraph_interrupt_valid(uint32_t num)
{
    nv3->pgraph.interrupt_enable_0 |= (1 << num);
    nv3_pmc_handle_interrupts(true);
}

// VBlank. Fired every single frame.
void nv3_pgraph_vblank_start(svga_t* svga)
{
    nv3_pgraph_interrupt_valid(NV3_PGRAPH_INTR_EN_0_VBLANK);
}