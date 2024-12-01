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
 *          Copyright 2024 starfrost
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
    { NV3_PGRAPH_INTR_0, "PGRAPH Interrupt Status 0", NULL, NULL },
    { NV3_PGRAPH_INTR_EN_0, "PGRAPH Interrupt Enable 0", NULL, NULL },
    { NV3_PGRAPH_INTR_1, "PGRAPH Interrupt Status 1", NULL, NULL },
    { NV3_PGRAPH_INTR_EN_1, "PGRAPH Interrupt Enable 1", NULL, NULL },
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

    nv_register_t* reg = nv_get_register(address, pgraph_registers, sizeof(pgraph_registers)/sizeof(pgraph_registers[0]));

    // todo: friendly logging
    
    nv_log("NV3: PGRAPH Read from 0x%08x", address);

    // if the register actually exists
    if (reg)
    {
        if (reg->friendly_name)
            nv_log(": %s\n", reg->friendly_name);
        else   
            nv_log("\n");

        // on-read function
        if (reg->on_read)
            return reg->on_read();
        else
        {
            switch (reg->address)
            {
                //interrupt status and enable regs
                case NV3_PGRAPH_INTR_0:
                    return nv3->pgraph.interrupt_status_0;
                case NV3_PGRAPH_INTR_1:
                    return nv3->pgraph.interrupt_status_1;
                case NV3_PGRAPH_INTR_EN_0:
                    return nv3->pgraph.interrupt_enable_0;
                case NV3_PGRAPH_INTR_EN_1:
                    return nv3->pgraph.interrupt_enable_1;
            }
        }

    }

    return 0x0; 
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

    nv_log("NV3: pgraph Write 0x%08x -> 0x%08x\n", value, address);

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