/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PMC - Master control for the chip
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
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

void nv3_pmc_init(void)
{
    nv_log("Initialising PMC...\n");

    if (nv3->nvbase.gpu_revision == NV3_PCI_CFG_REVISION_A00)
        nv3->pmc.boot = NV3_BOOT_REG_REV_A00;
    else if (nv3->nvbase.gpu_revision == NV3_PCI_CFG_REVISION_B00)
        nv3->pmc.boot = NV3_BOOT_REG_REV_B00;
    else 
        nv3->pmc.boot = NV3_BOOT_REG_REV_C00;

    nv3->pmc.intr_en = NV3_PMC_INTR_EN_HARDWARE | NV3_PMC_INTR_EN_SOFTWARE;

    nv_log("Initialising PMC: Done\n");
}

void nv3_pmc_clear_interrupts(void)
{
    nv_log_verbose_only("Clearing IRQs\n");
    pci_clear_irq(nv3->nvbase.pci_slot, PCI_INTA, &nv3->nvbase.pci_irq_state);
}

// Handle hardware interrupts
// We only clear when we need to, in other functions...
uint32_t nv3_pmc_handle_interrupts(bool send_now)
{
    // TODO:
    // PGRAPH DMA INTR_EN (there is no DMA engine yet)
    // Real-Mode Compatibility Interrupts

    uint32_t new_intr_value = 0x00;

    // set the new interrupt value
    
    // the registers are designed to line up so you can enable specific interrupts

    // Check Mediaport interrupts
    if (nv3->pme.intr & nv3->pme.intr_en)
        new_intr_value |= (NV3_PMC_INTERRUPT_PMEDIA_PENDING << NV3_PMC_INTERRUPT_PMEDIA);

    // Check FIFO interrupts
    if (nv3->pfifo.intr & nv3->pfifo.intr_en)
        new_intr_value |= (NV3_PMC_INTERRUPT_PFIFO_PENDING << NV3_PMC_INTERRUPT_PFIFO);

    // PFB interrupt is VBLANK PGRAPH interrupt...what nvidia...
    if (nv3->pgraph.intr_0 & (1 << 8)
    && nv3->pgraph.intr_en_0 & (1 << 8))
        new_intr_value |= (NV3_PMC_INTERRUPT_PFB_PENDING << NV3_PMC_INTERRUPT_PFB);

    if (nv3->pgraph.intr_0 & nv3->pgraph.intr_en_0 & ~(1 << 8)) // otherwise PGRAPH-0 interrupt
        new_intr_value |= (NV3_PMC_INTERRUPT_PGRAPH0_PENDING << NV3_PMC_INTERRUPT_PGRAPH0);

    /* PDMA (PGRAPH's DMA engine) is PMC bit 13; PGRAPH's INVALID register reaches PMC through INTR_0 bit 0 */
    if (nv3->pgraph.intr_dma & nv3->pgraph.intr_en_dma)
        new_intr_value |= (NV3_PMC_INTERRUPT_PGRAPH1_PENDING << NV3_PMC_INTERRUPT_PGRAPH1);

    // check video overlay interrupts
    if (nv3->pvideo.intr & nv3->pvideo.intr_en)
        new_intr_value |= (NV3_PMC_INTERRUPT_PVIDEO_PENDING << NV3_PMC_INTERRUPT_PVIDEO);

    // check PIT interrupts
    if (nv3->ptimer.intr & nv3->ptimer.intr_en)
        new_intr_value |= (NV3_PMC_INTERRUPT_PTIMER_PENDING << NV3_PMC_INTERRUPT_PTIMER);

    // check bus interrupts
    if (nv3->pbus.intr & nv3->pbus.intr_en)
        new_intr_value |= (NV3_PMC_INTERRUPT_PBUS_PENDING << NV3_PMC_INTERRUPT_PBUS);

    // check SW interrupts
    if (nv3->pmc.intr & (1 << NV3_PMC_INTERRUPT_SOFTWARE))
        new_intr_value |= (NV3_PMC_INTERRUPT_SOFTWARE_PENDING << NV3_PMC_INTERRUPT_SOFTWARE);

    nv3->pmc.intr = new_intr_value;

    // ***TODO: DOes INTR still change if INTR_EN=0???***
    // If interrupts are disabled don't bother

    if (!nv3->pmc.intr_en)
    {
        nv3_pmc_clear_interrupts();
        return nv3->pmc.intr;
    }
        

    // if we actually need to send the interrupt (i.e. this is a write) send it now
    if (send_now)
    {
        // no interrupts to send
        if (!(nv3->pmc.intr)
         || nv3->pmc.intr & 0x80000000) // software interrupt
        {
            nv3_pmc_clear_interrupts();
            return nv3->pmc.intr;
        }
            
        if ((nv3->pmc.intr & 0x7FFFFFFF))
        {
            if (nv3->pmc.intr_en & NV3_PMC_INTR_EN_HARDWARE)
            {
                nv_log_verbose_only("Firing hardware-originated interrupt NV3_PMC_INTR_0=0x%08x\n", nv3->pmc.intr);
                pci_set_irq(nv3->nvbase.pci_slot, PCI_INTA, &nv3->nvbase.pci_irq_state);
            }
            else
                nv_log_verbose_only("NOT firing hardware-originated interrupt NV3_PMC_INTR_0=0x%08x, BECAUSE HARDWARE INTERRUPTS ARE DISABLED\n", nv3->pmc.intr);      
        }
        else   
        {
            if (nv3->pmc.intr_en & NV3_PMC_INTR_EN_SOFTWARE)
            {
                nv_log_verbose_only("Firing software-originated interrupt NV3_PMC_INTR_0=0x%08x\n", nv3->pmc.intr);
                pci_set_irq(nv3->nvbase.pci_slot, PCI_INTA, &nv3->nvbase.pci_irq_state);
            }
            else
                nv_log_verbose_only("NOT firing software-originated interrupt NV3_PMC_INTR_0=0x%08x, BECAUSE SOFTWARE INTERRUPTS ARE DISABLED\n", nv3->pmc.intr); 
        }
    }

    return nv3->pmc.intr;
}

//
// ****** Read/Write functions start ******
//

uint32_t nv3_pmc_read(uint32_t address) 
{ 
    uint32_t ret = 0x00;

    switch (address)
    {
        case NV3_PMC_BOOT:          
            ret = nv3->pmc.boot;
            break;
        case NV3_PMC_INTR:
            /* Reading has no side effects: the line follows the pending, enabled sources */
            ret = nv3_pmc_handle_interrupts(false);
            break;
        case NV3_PMC_INTR_EN:
            //TODO: ACTUALLY CHANGE THE INTERRUPT STATE
            ret = nv3->pmc.intr_en;
            break;
        case NV3_PMC_ENABLE:
            ret = nv3->pmc.enable;
            break;
    }

    return ret; 
}

void nv3_pmc_write(uint32_t address, uint32_t value) 
{
    switch (address)
    {
        case NV3_PMC_INTR:
            // This can only be done by software interrupts...
            if (!(nv3->pmc.intr & 0x7FFFFFFF))
            {
                nv_warning("Huh? This is a hardware interrupt...Please use the INTR_EN registers of the GPU subsystem you want to trigger "
                " an interrupt on, rather than writing to NV3_PMC_INTR (Or this is a bug)...NV3_PMC_INTR=0x%08x)\n", nv3->pmc.intr_en);
                return; 
            }
            
            nv3_pmc_handle_interrupts(true);
            nv3->pmc.intr = value;
            break;
        case NV3_PMC_INTR_EN:
            nv3->pmc.intr_en = value & 0x03;
            nv3_pmc_handle_interrupts(value != 0);
            break;
        case NV3_PMC_ENABLE:
            nv3->pmc.enable = value;
            break;
    }
}