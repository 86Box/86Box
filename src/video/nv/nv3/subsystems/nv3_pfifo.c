/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PFIFO (FIFO for graphics object submission)
 *          PIO object submission
 *          Gray code conversion routines
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 starfrost
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

//
// ****** PFIFO register list START ******
//

nv_register_t pfifo_registers[] = {
    { NV3_PFIFO_INTR, "PFIFO - Interrupt Status", NULL, NULL},
    { NV3_PFIFO_INTR_EN, "PFIFO - Interrupt Enable", NULL, NULL,},
    { NV3_PFIFO_DELAY_0, "PFIFO - DMA Delay/Retry Register", NULL, NULL},
    { NV3_PFIFO_DEBUG_0, "PFIFO - Debug 0", NULL, NULL, }, 
    { NV3_PFIFO_CONFIG_0, "PFIFO - Config 0", NULL, NULL, },
    { NV3_PFIFO_CONFIG_RAMFC, "PFIFO - RAMIN RAMFC Config", NULL, NULL },
    { NV3_PFIFO_CONFIG_RAMHT, "PFIFO - RAMIN RAMHT Config", NULL, NULL },
    { NV3_PFIFO_CONFIG_RAMRO, "PFIFO - RAMIN RAMRO Config", NULL, NULL },
    { NV3_PFIFO_CACHE_REASSIGNMENT, "PFIFO - Allow Cache Channel Reassignment", NULL, NULL },
    { NV3_PFIFO_CACHE0_PULLER_CONTROL, "PFIFO - Cache0 Puller Control", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULL0, "PFIFO - Cache1 Puller Control"},
    { NV3_PFIFO_CACHE0_PULLER_CTX_STATE, "PFIFO - Cache0 Puller State1 (Is context clean?)", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULL0, "PFIFO - Cache1 Puller State0", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULLER_CTX_STATE, "PFIFO - Cache1 Puller State1 (Is context clean?)", NULL, NULL},
    { NV3_PFIFO_CACHE0_DMA_PUSH0, "PFIFO - Cache0 Access", NULL, NULL, },
    { NV3_PFIFO_CACHE1_DMA_PUSH0, "PFIFO - Cache1 Access", NULL, NULL, },
    { NV3_PFIFO_CACHE0_PUSH_CHANNEL_ID, "PFIFO - Cache0 DMA Channel ID", NULL, NULL, },
    { NV3_PFIFO_CACHE1_PUSH_CHANNEL_ID, "PFIFO - Cache1 DMA Channel ID", NULL, NULL, },
    { NV3_PFIFO_CACHE0_ERROR_PENDING, "PFIFO - Cache0 DMA Error Pending?", NULL, NULL, },
    { NV3_PFIFO_CACHE0_STATUS, "PFIFO - Cache0 Status", NULL, NULL},
    { NV3_PFIFO_CACHE1_STATUS, "PFIFO - Cache1 Status", NULL, NULL}, 
    { NV3_PFIFO_CACHE0_GET, "PFIFO - Cache0 Get", NULL, NULL },
    { NV3_PFIFO_CACHE1_GET, "PFIFO - Cache1 Get", NULL, NULL },
    { NV3_PFIFO_CACHE0_PUT, "PFIFO - Cache0 Put", NULL, NULL },
    { NV3_PFIFO_CACHE1_PUT, "PFIFO - Cache1 Put", NULL, NULL },
    //Cache1 exclusive stuff
    { NV3_PFIFO_CACHE1_DMA_CONFIG_0, "PFIFO - Cache1 DMA Config0"},
    { NV3_PFIFO_CACHE1_DMA_CONFIG_1, "PFIFO - Cache1 DMA Config1"},
    { NV3_PFIFO_CACHE1_DMA_CONFIG_2, "PFIFO - Cache1 DMA Config2"},
    { NV3_PFIFO_CACHE1_DMA_CONFIG_3, "PFIFO - Cache1 DMA Config3"},
    { NV3_PFIFO_CACHE1_DMA_STATUS, "PFIFO - Cache1 DMA Status - PROBABLY TRIGGERING DMA"},
    { NV3_PFIFO_CACHE1_DMA_TLB_PT_BASE, "PFIFO - Cache1 DMA Translation Lookaside Buffer - Pagetable Base"},
    { NV3_PFIFO_CACHE1_DMA_TLB_PTE, "PFIFO - Cache1 DMA Status"},
    { NV3_PFIFO_CACHE1_DMA_TLB_TAG, "PFIFO - Cache1 DMA Status"},
    //Runout
    { NV3_PFIFO_RUNOUT_GET, "PFIFO Runout Get Address [8:3 if 512b, otherwise 12:3]"},
    { NV3_PFIFO_RUNOUT_PUT, "PFIFO Runout Put Address [8:3 if 512b, otherwise 12:3]"},
    { NV3_PFIFO_RUNOUT_STATUS, "PFIFO Runout Status"},
    { NV_REG_LIST_END, NULL, NULL, NULL}, // sentinel value 
};

// PFIFO init code
void nv3_pfifo_init()
{
    nv_log("Initialising PFIFO...");

    nv_log("Done!\n");    
}

uint32_t nv3_pfifo_read(uint32_t address) 
{ 
    // before doing anything, check the subsystem enablement state

    if (!(nv3->pmc.enable >> NV3_PMC_ENABLE_PFIFO)
    & NV3_PMC_ENABLE_PFIFO_ENABLED)
    {
        nv_log("Repressing PFIFO read. The subsystem is disabled according to pmc_enable, returning 0\n");
        return 0x00;
    }

    uint32_t ret = 0x00;

    nv_register_t* reg = nv_get_register(address, pfifo_registers, sizeof(pfifo_registers)/sizeof(pfifo_registers[0]));

    // todo: friendly logging
    
    nv_log("PFIFO Read from 0x%08x", address);

    // if the register actually exists
    if (reg)
    {

        // on-read function
        if (reg->on_read)
            ret = reg->on_read();
        else
        {   
            // Interrupt state:
            // Bit 0 - Cache Error
            // Bit 4 - RAMRO Triggered
            // Bit 8 - RAMRO Overflow (too many invalid dma objects)
            // Bit 12 - DMA Pusher 
            // Bit 16 - DMA Page Table Entry (pagefault?)
            switch (reg->address)
            {
                case NV3_PFIFO_INTR:
                    ret = nv3->pfifo.interrupt_status;
                    break;
                case NV3_PFIFO_INTR_EN:
                    ret = nv3->pfifo.interrupt_enable;
                    break;
                case NV3_PFIFO_DELAY_0:
                    ret = nv3->pfifo.dma_delay_retry;
                    break;
                // Debug
                case NV3_PFIFO_DEBUG_0:
                    ret = nv3->pfifo.debug_0;
                    break;
                case NV3_PFIFO_CONFIG_0:
                    ret = nv3->pfifo.config_0;
                    break; 
                // Some of these may need to become functions.
                case NV3_PFIFO_CONFIG_RAMFC:
                    ret = nv3->pfifo.ramfc_config;
                    break;
                case NV3_PFIFO_CONFIG_RAMHT:
                    ret = nv3->pfifo.ramht_config;
                    break;
                case NV3_PFIFO_CONFIG_RAMRO:
                    ret = nv3->pfifo.ramro_config;
                    break;
                case NV3_PFIFO_CACHE0_PULLER_CONTROL:
                    ret = nv3->pfifo.cache0_settings.puller_control;
                    break;
                case NV3_PFIFO_CACHE1_PULL0:
                    ret = nv3->pfifo.cache1_settings.puller_control;
                    break;
                case NV3_PFIFO_CACHE0_PULLER_CTX_STATE:
                    ret = (nv3->pfifo.cache0_settings.context_is_dirty) ? (1 << NV3_PFIFO_CACHE0_PULLER_CTX_STATE_DIRTY) : 0;
                    break;
                case NV3_PFIFO_CACHE1_PULLER_CTX_STATE:
                    ret = (nv3->pfifo.cache0_settings.context_is_dirty) ? (1 << NV3_PFIFO_CACHE0_PULLER_CTX_STATE_DIRTY) : 0;
                    break;
                case NV3_PFIFO_CACHE0_DMA_PUSH0:
                    ret = nv3->pfifo.cache0_settings.dma_push0;
                    break;
                case NV3_PFIFO_CACHE1_DMA_PUSH0:
                    ret = nv3->pfifo.cache1_settings.dma_push0;
                    break; 
                case NV3_PFIFO_CACHE0_PUSH_CHANNEL_ID:
                    ret = nv3->pfifo.cache0_settings.channel;
                    break;
                case NV3_PFIFO_CACHE1_PUSH_CHANNEL_ID:
                    ret = nv3->pfifo.cache1_settings.channel;
                    break;
                case NV3_PFIFO_CACHE0_STATUS:  
                    // CACHE0 has only one entry so it can only ever be empty or full

                    if (nv3->pfifo.cache0_settings.put_address == nv3->pfifo.cache1_settings.get_address)
                        ret |= 1 << NV3_PFIFO_CACHE0_STATUS_EMPTY;
                    else
                        ret |= 1 << NV3_PFIFO_CACHE0_STATUS_FULL;
    
                    break;
                case NV3_PFIFO_CACHE1_STATUS:
                    // CACHE1 doesn't...

                    if (nv3->pfifo.cache1_settings.put_address == nv3->pfifo.cache1_settings.get_address)
                        ret |= 1 << NV3_PFIFO_CACHE1_STATUS_EMPTY;

                    // Check if Cache1 (0x7C bytes in size depending on gpu?) is full
                    // Based on how the drivers do it
                    if (!nv3_pfifo_cache1_num_free_spaces())
                        ret |= 1 << NV3_PFIFO_CACHE1_STATUS_FULL;
                    
                    if (nv3->pfifo.runout_put != nv3->pfifo.runout_get)
                        ret |= 1 << NV3_PFIFO_CACHE1_STATUS_RANOUT;

                    break;
                case NV3_PFIFO_CACHE0_METHOD:
                    ret = ((nv3->pfifo.cache0_settings.method_subchannel << 13) & 0x07)
                    | ((nv3->pfifo.cache0_settings.method_address << 2) & 0x7FF);
                    break;
                case NV3_PFIFO_CACHE1_METHOD:
                    ret = ((nv3->pfifo.cache1_settings.method_subchannel << 13) & 0x07)
                    | ((nv3->pfifo.cache1_settings.method_address << 2) & 0x7FF);
                    break;
                case NV3_PFIFO_CACHE0_GET:
                    //wa
                    break;
                // Reassignment
                case NV3_PFIFO_CACHE_REASSIGNMENT:
                    ret = nv3->pfifo.cache_reassignment & 0x01; //1bit meaningful
                    break;
                // Cache1 exclusive stuff
                // Control
                case NV3_PFIFO_CACHE1_DMA_CONFIG_0:
                    ret = nv3->pfifo.cache1_settings.dma_state;
                    break; 
                case NV3_PFIFO_CACHE1_DMA_CONFIG_1:
                    ret = nv3->pfifo.cache1_settings.dma_length & (VRAM_SIZE_8MB) - 4; //MAX vram size
                    break;
                case NV3_PFIFO_CACHE1_DMA_CONFIG_2:
                    ret = nv3->pfifo.cache1_settings.dma_address;
                    break;
                case NV3_PFIFO_CACHE1_DMA_CONFIG_3:
                    if (nv3->nvbase.bus_generation == nv_bus_pci)
                        return NV3_PFIFO_CACHE1_DMA_CONFIG_3_TARGET_NODE_PCI;
                    else 
                        return NV3_PFIFO_CACHE1_DMA_CONFIG_3_TARGET_NODE_AGP;
                    break;
                case NV3_PFIFO_CACHE1_DMA_STATUS:
                    ret = nv3->pfifo.cache1_settings.dma_status;
                    break;
                case NV3_PFIFO_CACHE1_DMA_TLB_PT_BASE:
                    ret = nv3->pfifo.cache1_settings.dma_tlb_pt_base;
                    break;
                case NV3_PFIFO_CACHE1_DMA_TLB_PTE:
                    ret = nv3->pfifo.cache1_settings.dma_tlb_pte;
                    break;
                case NV3_PFIFO_CACHE1_DMA_TLB_TAG:
                    ret = nv3->pfifo.cache1_settings.dma_tlb_tag;
                    break;
                // Runout
                case NV3_PFIFO_RUNOUT_GET:
                    ret = nv3->pfifo.runout_get;
                    break;
                case NV3_PFIFO_RUNOUT_PUT:
                    ret = nv3->pfifo.runout_put;
                    break;
                case NV3_PFIFO_RUNOUT_STATUS:
                    if (nv3->pfifo.runout_put == nv3->pfifo.runout_get)
                        ret |= 1 << NV3_PFIFO_RUNOUT_STATUS_EMPTY; /* good news */
                    else 
                        ret |= 1 << NV3_PFIFO_RUNOUT_STATUS_RANOUT; /* bad news */

                    /* TODO: the following code sucks (move to a functio?) */

                    uint32_t new_size_ramro = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

                    if (new_size_ramro == 0)
                        new_size_ramro = 0x200;
                    else if (new_size_ramro == 1)
                        new_size_ramro = 0x2000;
                    
                    // WTF?
                    if (nv3->pfifo.runout_put + 0x08 & (new_size_ramro - 0x08) == nv3->pfifo.runout_get)
                        ret |= 1 << NV3_PFIFO_RUNOUT_STATUS_FULL; /* VERY BAD news */

                    break;
                
                /* Cache1 is handled below */
                case NV3_PFIFO_CACHE0_CTX:
                    ret = nv3->pfifo.cache0_settings.context[0];
                    break;
                
            }
        }

        if (reg->friendly_name)
            nv_log(": 0x%08x <- %s\n", ret, reg->friendly_name);
        else   
            nv_log("\n");
    }
    /* Handle some special memory areas */
    else if (address >= NV3_PFIFO_CACHE1_CTX_START && address <= NV3_PFIFO_CACHE1_CTX_END)
    {
        uint32_t ctx_entry_id = ((address - NV3_PFIFO_CACHE1_CTX_START) / 16) % 8;
        ret = nv3->pfifo.cache1_settings.context[ctx_entry_id];

        nv_log("PFIFO Cache1 CTX Read Entry=%d Value=0x%04x", ctx_entry_id, ret);
    }
    else
    {
        nv_log(": Unknown register read (address=0x%08x), returning 0x00\n", address);
    }

    return ret; 
}

void nv3_pfifo_trigger_dma_if_required()
{
    // Not a thing for cache0
    
    bool cache1_dma = false;

    /* Check that DMA is enabled */
    if (nv3->pfifo.cache1_settings.dma_state
    && nv3->pfifo.cache1_settings.dma_enabled)
    {
        uint32_t bytes_to_send = nv3->pfifo.cache1_settings.dma_length;
        uint32_t where_to_send = nv3->pfifo.cache1_settings.dma_address;
        uint32_t target_node = nv3->pfifo.cache1_settings.dma_target_node; //2=pci, 3=agp. What does this even do

        /* Pagetable information */
        uint32_t tlb_pt_base = nv3->pfifo.cache1_settings.dma_tlb_pt_base;
        uint32_t tlb_pt_entry = nv3->pfifo.cache1_settings.dma_tlb_pte;
        uint32_t tlb_pt_tag = nv3->pfifo.cache1_settings.dma_tlb_tag; // 0xFFFFFFFF usually?

        /* PUSH - System to GPU (?) */
        if (nv3->pfifo.cache1_settings.dma_push0)
        {

        }

        /* PULL - GPU to System */
        nv_log("Initiating NV to System DMA - Probably we are trying to notify");
    }
}

void nv3_pfifo_write(uint32_t address, uint32_t value) 
{
    // before doing anything, check the subsystem enablement

    if (!(nv3->pmc.enable >> NV3_PMC_ENABLE_PFIFO)
    & NV3_PMC_ENABLE_PFIFO_ENABLED)
    {
        nv_log("Repressing PFIFO write. The subsystem is disabled according to pmc_enable\n");
        return;
    }

    nv_register_t* reg = nv_get_register(address, pfifo_registers, sizeof(pfifo_registers)/sizeof(pfifo_registers[0]));

    nv_log("PFIFO Write 0x%08x -> 0x%08x", value, address);

    // if the register actually exists
    if (reg)
    {
        // on-read function
        if (reg->on_write)
            reg->on_write(value);
        else
        {
            switch (reg->address)
            {
                // Interrupt state:
                // Bit 0 - Cache Error
                // Bit 4 - RAMRO Triggered
                // Bit 8 - RAMRO Overflow (too many invalid dma objects)
                // Bit 12 - DMA Pusher 
                // Bit 16 - DMA Page Table Entry (pagefault?)
                case NV3_PFIFO_INTR:
                    nv3->pfifo.interrupt_status &= ~value;
                    nv3_pmc_clear_interrupts();

                    // update the internal cache error state
                    if (!nv3->pfifo.interrupt_status & NV3_PFIFO_INTR_CACHE_ERROR)
                        nv3->pfifo.debug_0 &= ~NV3_PFIFO_INTR_CACHE_ERROR;
                    break;
                case NV3_PFIFO_INTR_EN:
                    nv3->pfifo.interrupt_enable = value & 0x00011111;
                    nv3_pmc_handle_interrupts(true);
                    break;
                case NV3_PFIFO_DELAY_0:
                    nv3->pfifo.dma_delay_retry = value;
                    break;
                case NV3_PFIFO_CONFIG_0:
                    nv3->pfifo.config_0 = value;
                    break;

                case NV3_PFIFO_CONFIG_RAMHT:
                    nv3->pfifo.ramht_config = value;
// This code sucks a bit fix it later
#ifdef ENABLE_NV_LOG
                    uint32_t new_size_ramht = ((value >> 16) & 0x03);

                    if (new_size_ramht == 0)
                        new_size_ramht = 0x1000;
                    else if (new_size_ramht == 1)
                        new_size_ramht = 0x2000;
                    else if (new_size_ramht == 2)
                        new_size_ramht = 0x4000;
                    else if (new_size_ramht == 3)
                        new_size_ramht = 0x8000;  

                    nv_log("RAMHT Reconfiguration\n"
                    "Base Address in RAMIN: %d\n"
                    "Size: 0x%08x bytes\n", ((nv3->pfifo.ramht_config >> NV3_PFIFO_CONFIG_RAMHT_BASE_ADDRESS) & 0x0F) << 12, new_size_ramht); 
#endif
                    break;
                case NV3_PFIFO_CONFIG_RAMFC:
                    nv3->pfifo.ramfc_config = value;

                    nv_log("RAMFC Reconfiguration\n"
                    "Base Address in RAMIN: %d\n", ((nv3->pfifo.ramfc_config >> NV3_PFIFO_CONFIG_RAMFC_BASE_ADDRESS) & 0x7F) << 9); 
                    break;
                case NV3_PFIFO_CONFIG_RAMRO:
                    nv3->pfifo.ramro_config = value;

                    uint32_t new_size_ramro = ((value >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

                    if (new_size_ramro == 0)
                        new_size_ramro = 0x200;
                    else if (new_size_ramro == 1)
                        new_size_ramro = 0x2000;
                    
                    nv_log("RAMRO Reconfiguration\n"
                    "Base Address in RAMIN: %d\n"
                    "Size: 0x%08x bytes\n", ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_BASE_ADDRESS) & 0x7F) << 9, new_size_ramro); 
                    break;
                case NV3_PFIFO_DEBUG_0:
                    nv3->pfifo.debug_0 = value;
                    break;
                // Reassignment
                case NV3_PFIFO_CACHE_REASSIGNMENT:
                    nv3->pfifo.cache_reassignment = value & 0x01; //1bit meaningful
                    break;
                // Control
                case NV3_PFIFO_CACHE0_PULLER_CONTROL:
                    nv3->pfifo.cache0_settings.puller_control = value; // 8bits meaningful
                    break;
                case NV3_PFIFO_CACHE1_PULL0:
                    nv3->pfifo.cache1_settings.puller_control = value; // 8bits meaningful
                    break;
                case NV3_PFIFO_CACHE0_PULLER_CTX_STATE:
                    nv3->pfifo.cache0_settings.context_is_dirty = (value >> NV3_PFIFO_CACHE0_PULLER_CTX_STATE_DIRTY) & 0x01;
                    break;
                case NV3_PFIFO_CACHE1_PULLER_CTX_STATE:
                    nv3->pfifo.cache1_settings.context_is_dirty = (value >> NV3_PFIFO_CACHE0_PULLER_CTX_STATE_DIRTY) & 0x01;
                    break;
                case NV3_PFIFO_CACHE0_DMA_PUSH0:
                    nv3->pfifo.cache0_settings.dma_push0 = value;
                    break;
                case NV3_PFIFO_CACHE1_DMA_PUSH0:
                    nv3->pfifo.cache1_settings.dma_push0 = value;
                    break; 
                case NV3_PFIFO_CACHE0_PUSH_CHANNEL_ID:
                    nv3->pfifo.cache0_settings.channel = value;
                    break;
                case NV3_PFIFO_CACHE1_PUSH_CHANNEL_ID:
                    nv3->pfifo.cache1_settings.channel = value;
                    break;
                // CACHE0_STATUS and CACHE1_STATUS are not writable
                case NV3_PFIFO_CACHE0_METHOD:
                    nv3->pfifo.cache0_settings.method_subchannel = (value >> 13) & 0x07;
                    nv3->pfifo.cache0_settings.method_address = (value >> 2) & 0x7FF;
                    break;
                case NV3_PFIFO_CACHE1_METHOD:
                    nv3->pfifo.cache1_settings.method_subchannel = (value >> 13) & 0x07;
                    nv3->pfifo.cache1_settings.method_address = (value >> 2) & 0x7FF;
                    break;
                case NV3_PFIFO_CACHE1_DMA_CONFIG_0:
                    nv3->pfifo.cache1_settings.dma_state = value;
                    break; 
                case NV3_PFIFO_CACHE1_DMA_CONFIG_1:
                    nv3->pfifo.cache1_settings.dma_length = value;
                    break;
                case NV3_PFIFO_CACHE1_DMA_CONFIG_2:
                    nv3->pfifo.cache1_settings.dma_address = value;
                    break;
                case NV3_PFIFO_CACHE1_DMA_STATUS:
                    nv3->pfifo.cache1_settings.dma_status = value;
                    break;
                case NV3_PFIFO_CACHE1_DMA_TLB_PT_BASE:
                    nv3->pfifo.cache1_settings.dma_tlb_pt_base = value;
                    break;
                case NV3_PFIFO_CACHE1_DMA_TLB_PTE:
                    nv3->pfifo.cache1_settings.dma_tlb_pte = value;
                    break;
                case NV3_PFIFO_CACHE1_DMA_TLB_TAG:
                    nv3->pfifo.cache1_settings.dma_tlb_tag = value;
                    break;
                case NV3_PFIFO_RUNOUT_GET:
                    uint32_t size_get = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

                    if (size_get == 0) //512b
                        nv3->pfifo.runout_get = ((value & 0x3F) << 3);
                    else 
                        nv3->pfifo.runout_get = ((value & 0x3FF) << 3);
                    break;
                case NV3_PFIFO_RUNOUT_PUT:
                    uint32_t size_put = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

                    if (size_put == 0) //512b
                        nv3->pfifo.runout_put = ((value & 0x3F) << 3);
                    else 
                        nv3->pfifo.runout_put = ((value & 0x3FF) << 3);
                    break;
                /* Cache1 is handled below */
                case NV3_PFIFO_CACHE0_CTX:
                    nv3->pfifo.cache0_settings.context[0] = value;
                    break;
            }
        }

        if (reg->friendly_name)
            nv_log(": %s\n", reg->friendly_name);
        else   
            nv_log("\n");
    }
    /* Handle some special memory areas */
    else if (address >= NV3_PFIFO_CACHE1_CTX_START && address <= NV3_PFIFO_CACHE1_CTX_END)
    {
        uint32_t ctx_entry_id = ((address - NV3_PFIFO_CACHE1_CTX_START) / 16) % 8;
        nv3->pfifo.cache1_settings.context[ctx_entry_id] = value;

        nv_log("PFIFO Cache1 CTX Write Entry=%d value=0x%04x\n", ctx_entry_id, value);
    }

    /* Trigger DMA for notifications if we need to */
    nv3_pfifo_trigger_dma_if_required();
}

/* 
https://en.wikipedia.org/wiki/Gray_code
WHY?????? IT'S NOT A TELEGRAPH IT'S A GPU?????

Convert from a normal number to a total insanity number which is only used in PFIFO CACHE1 for ungodly and totally unknowable reasons 
*/
uint32_t nv3_pfifo_cache1_normal2gray(uint32_t val)
{
    return (val) ^ (val >> 1);
}

/* 
Back to sanity
*/
uint32_t nv3_pfifo_cache1_gray2normal(uint32_t val)
{
    uint32_t mask = val >> 1;

    // shift right until we have our normla number again
    while (mask)
    {
        // Algorithm from NT4 drivers, version 1.29
        mask >>= 1; 
        val ^= mask;
    }

    return val;
}

// Submits graphics objects INTO cache0
void nv3_pfifo_cache0_push()
{
    
}

// Pulls graphics objects OUT of cache0
void nv3_pfifo_cache0_pull()
{
    // Do nothing if PFIFO CACHE0 is disabled
    if (!nv3->pfifo.cache0_settings.puller_control & (1 >> NV3_PFIFO_CACHE0_PULLER_CONTROL_ENABLED))
        return; 

    // Do nothing if there is nothing in cache0 to pull
    if (nv3->pfifo.cache0_settings.put_address == nv3->pfifo.cache0_settings.get_address)
        return;

    // There is only one entry for cache0 
    uint8_t current_channel = nv3->pfifo.cache0_settings.channel;
    uint8_t current_subchannel = nv3->pfifo.cache0_entry.subchannel;
    uint32_t current_name = nv3->pfifo.cache0_entry.data;
    uint16_t current_method = nv3->pfifo.cache0_entry.method;

    // i.e. there is no method in cache0, so we have to find the object.
    if (!current_method)
    {
        if (!nv3_ramin_find_object(current_name, 0, current_channel, current_subchannel))
            return; // interrupt was fired, and we went to ramro
    }

    uint32_t current_context = nv3->pfifo.cache0_settings.context[0]; // only 1 entry for CACHE0 so basically ignore the other context entries?
    uint8_t class_id = ((nv3_ramin_context_t*)&current_context)->class_id;

    // Tell the CPU if we found a software method
    if (current_context & 0x800000)
    {
        nv3->pfifo.cache0_settings.puller_control |= NV3_PFIFO_CACHE0_PULLER_CONTROL_SOFTWARE_METHOD;
        nv3->pfifo.cache0_settings.puller_control &= ~NV3_PFIFO_CACHE0_PULLER_CONTROL_ENABLED;
        nv3_pfifo_interrupt(NV3_PFIFO_INTR_CACHE_ERROR, true);
    }

    // Is this needed?
    nv3->pfifo.cache0_settings.get_address ^= 0x04;

    #ifndef RELEASE_BUILD
    nv_log("***** SUBMITTING GRAPHICS COMMANDS CURRENTLY UNIMPLEMENTED - CACHE0 PULLED ****** Contextual information below\n");
            
    nv3_debug_ramin_print_context_info(current_name, *(nv3_ramin_context_t*)current_context);
    #endif

}

void nv3_pfifo_context_switch(uint32_t new_channel)
{
    /* Send our contexts to RAMFC. Load the new ones from RAMFC. */
    if (new_channel >= NV3_DMA_CHANNELS)
        fatal("Tried to switch to invalid dma channel");

    uint16_t ramfc_base = nv3->pfifo.ramfc_config >> NV3_PFIFO_CONFIG_RAMFC_BASE_ADDRESS & 0xF;

}

// NV_USER writes go here!
// Pushes graphics objects into cache1
void nv3_pfifo_cache1_push(uint32_t addr, uint32_t val)
{
    bool oh_shit = false;   // RAMRO needed
    nv3_ramin_ramro_reason oh_shit_reason = 0x00; // It's all good for now

    // bit 23 of a ramin dword means it's a write...
    uint32_t new_address = 0;

    uint32_t method_offset = (addr & 0x1FFC); // size of dma object is 0x2000 and some universal methods are implemented at this point, like free
    
    // Up to 128 per envytools?
    uint32_t channel = (addr >> NV3_OBJECT_SUBMIT_CHANNEL) & 0x7F;
    uint32_t subchannel = (addr >> NV3_OBJECT_SUBMIT_SUBCHANNEL) & (NV3_DMA_CHANNELS - 1);

    // first make sure there is even any cache available
    if (!nv3->pfifo.cache1_settings.dma_push0)
    {
        oh_shit = true; 
        oh_shit_reason = nv3_runout_reason_no_cache_available;
        new_address |= (nv3_runout_reason_no_cache_available << NV3_PFIFO_RUNOUT_RAMIN_ERR);

    }
    
    // Check if runout is full
    if (nv3->pfifo.runout_get != nv3->pfifo.runout_put)
    {
        oh_shit = true;
        oh_shit_reason = nv3_runout_reason_cache_ran_out; // ? really ? I guess this means we already ran out..
        new_address |= (nv3_runout_reason_cache_ran_out << NV3_PFIFO_RUNOUT_RAMIN_ERR);
    }

    if (!nv3_pfifo_cache1_num_free_spaces())
    {
        oh_shit = true;
        oh_shit_reason = nv3_runout_reason_free_count_overrun;
        new_address |= (nv3_runout_reason_free_count_overrun << NV3_PFIFO_RUNOUT_RAMIN_ERR);
    }

    // 0x0 is used for the context
    if (method_offset > 0 && method_offset < 0x100)
    {
        // Reserved NVIDIA Objects
        oh_shit = true; 
        oh_shit_reason = nv3_runout_reason_reserved_access;
        new_address |= (nv3_runout_reason_reserved_access << NV3_PFIFO_RUNOUT_RAMIN_ERR);

    }

    // Now check for context switching

    if (channel != nv3->pfifo.cache1_settings.channel)
    {
        // Cache reassignment required
        if (!nv3->pfifo.cache_reassignment 
        || (nv3->pfifo.cache1_settings.get_address != nv3->pfifo.cache1_settings.get_address))
        {
            oh_shit = true;
            oh_shit_reason = nv3_runout_reason_no_cache_available;
            new_address |= (nv3_runout_reason_no_cache_available << NV3_PFIFO_RUNOUT_RAMIN_ERR);
        }

        nv3_pfifo_context_switch(channel);
    }

    // Did we fuck up?
    if (oh_shit)
    {
        nv_log("WE ARE FUCKED Runout Error=%d Channel=%d Subchannel=%d Method=0x%04x IMPLEMENT THIS OR DIE!!!", oh_shit_reason, channel, subchannel, method_offset);
        return;
    }

    // We didn't. Let's put it in CACHE1
    uint32_t current_put_address = nv3->pfifo.cache1_settings.put_address >> 2;
    nv3->pfifo.cache1_entries[current_put_address].subchannel = subchannel;
    nv3->pfifo.cache1_entries[current_put_address].method = method_offset;
    nv3->pfifo.cache1_entries[current_put_address].data = val;

    // now we have to recalculate the cache1 put address
    uint32_t next_put_address = nv3_pfifo_cache1_gray2normal(current_put_address) + 1;

    if (nv3->nvbase.gpu_revision >= NV3_BOOT_REG_REV_C00) // RIVA 128ZX#
        next_put_address &= (NV3_PFIFO_CACHE1_SIZE_REV_C - 1);
    else 
        next_put_address &= (NV3_PFIFO_CACHE1_SIZE_REV_AB - 1);

    nv3->pfifo.cache1_settings.put_address = nv3_pfifo_cache1_normal2gray(next_put_address) << 2;

    nv_log("Submitted object [PIO]: Channel %d.%d, Method ID 0x%04x (Put Address is now %d)\n",
         channel, subchannel, method_offset, nv3->pfifo.cache1_settings.put_address);
   
    // Now we're done. Phew!
}

// Pulls graphics objects OUT of cache1
void nv3_pfifo_cache1_pull()
{
    // Do nothing if PFIFO CACHE1 is disabled
    if (!nv3->pfifo.cache1_settings.puller_control & (1 >> NV3_PFIFO_CACHE1_PULL0_ENABLED))
        return; 

    // Do nothing if there is nothing in cache1 to pull
    if (nv3->pfifo.cache1_settings.put_address == nv3->pfifo.cache1_settings.get_address)
        return;

    uint32_t get_index = nv3->pfifo.cache1_settings.get_address >> 2; // 32 bit aligned probably

    uint8_t current_channel = nv3->pfifo.cache1_settings.channel;
    uint8_t current_subchannel = nv3->pfifo.cache1_entries[get_index].subchannel;
    uint32_t current_name = nv3->pfifo.cache1_entries[get_index].data;
    uint16_t current_method = nv3->pfifo.cache1_entries[get_index].method;
  
    // NV_ROOT
    if (!current_method)
    {
        if (!nv3_ramin_find_object(current_name, 1, current_channel, current_subchannel))
            return; // interrupt was fired, and we went to ramro
    }

    uint32_t current_context = nv3->pfifo.cache1_settings.context[current_subchannel]; // get the current subchannel

    uint8_t class_id = ((nv3_ramin_context_t*)&current_context)->class_id;

    // Tell the CPU if we found a software method
    if (current_context & 0x800000)
    {
        nv3->pfifo.cache1_settings.puller_control |= NV3_PFIFO_CACHE0_PULLER_CONTROL_SOFTWARE_METHOD;
        nv3->pfifo.cache1_settings.puller_control &= ~NV3_PFIFO_CACHE0_PULLER_CONTROL_ENABLED;
        nv3_pfifo_interrupt(NV3_PFIFO_INTR_CACHE_ERROR, true);
    }

    // start by incrementing
    uint32_t next_get_address = nv3_pfifo_cache1_gray2normal(get_index) + 1;
    
    if (nv3->nvbase.gpu_revision >= NV3_BOOT_REG_REV_C00) // RIVA 128ZX#
        next_get_address &= (NV3_PFIFO_CACHE1_SIZE_REV_C - 1);
    else 
        next_get_address &= (NV3_PFIFO_CACHE1_SIZE_REV_AB - 1);

    // Is this needed?
    nv3->pfifo.cache1_settings.get_address = nv3_pfifo_cache1_normal2gray(next_get_address) << 2;

    #ifndef RELEASE_BUILD
    nv_log("***** OBJECT PULLED, SUBMITTING GRAPHICS COMMANDS CURRENTLY UNIMPLEMENTED - ****** Contextual information below\n");
            
    nv3_debug_ramin_print_context_info(current_name, *(nv3_ramin_context_t*)current_context);
    #endif

    //Todo: finish it
}

// THIS IS PER SUBCHANNEL!
uint32_t nv3_pfifo_cache1_num_free_spaces()
{
    // convert to gray code
    uint32_t real_get_address = nv3_pfifo_cache1_normal2gray(nv3->pfifo.cache1_settings.get_address);
    uint32_t real_put_address = nv3_pfifo_cache1_normal2gray(nv3->pfifo.cache1_settings.put_address);
    
    // There is no hope of being able to understand it. Nobody can understand
    return (real_get_address - real_put_address - 4) & 0x7C; // there are 64 entries what
}