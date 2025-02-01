/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 pfifo (FIFO for graphics object submission)
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
    { NV3_PFIFO_CACHE0_PULLER_CONTROL, "PFIFO - Cache0 Puller Control", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULLER_CONTROL, "PFIFO - Cache1 Puller Control"},
    { NV3_PFIFO_CACHE0_PULLER_CTX_IS_DIRTY, "PFIFO - Cache0 Puller State1 (Is context clean?)", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULLER_CONTROL, "PFIFO - Cache1 Puller State0", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULLER_STATE1, "PFIFO - Cache1 Puller State1 (Is context clean?)", NULL, NULL},
    { NV3_PFIFO_CACHE0_PUSH_ACCESS, "PFIFO - Cache0 Access", NULL, NULL, },
    { NV3_PFIFO_CACHE1_PUSH_ACCESS, "PFIFO - Cache1 Access", NULL, NULL, },
    { NV3_PFIFO_CACHE0_PUSH_CHANNEL_ID, "PFIFO - Cache0 DMA Channel ID", NULL, NULL, },
    { NV3_PFIFO_CACHE1_PUSH_CHANNEL_ID, "PFIFO - Cache1 DMA Channel ID", NULL, NULL, },
    { NV3_PFIFO_CACHE0_ERROR_PENDING, "PFIFO - Cache0 DMA Error Pending?", NULL, NULL, },
    { NV3_PFIFO_CACHE0_STATUS, "PFIFO - Cache0 Status", NULL, NULL},
    { NV3_PFIFO_CACHE1_STATUS, "PFIFO - Cache1 Status", NULL, NULL}, 
    { NV3_PFIFO_CACHE0_GET, "PFIFO - Cache0 Get MUST TRIGGER DMA NOW TO OBTAIN ENTRY", NULL, NULL },
    { NV3_PFIFO_CACHE1_GET, "PFIFO - Cache1 Get MUST TRIGGER DMA NOW TO OBTAIN ENTRY", NULL, NULL },
    { NV3_PFIFO_CACHE0_PUT, "PFIFO - Cache0 Put MUST TRIGGER DMA NOW TO INSERT ENTRY", NULL, NULL },
    { NV3_PFIFO_CACHE1_PUT, "PFIFO - Cache1 Put MUST TRIGGER DMA NOW TO INSERT ENTRY", NULL, NULL },
    //Cache1 exclusive stuff
    { NV3_PFIFO_CACHE1_DMA_CONFIG_0, "PFIFO - Cache1 DMA Config0"},
    { NV3_PFIFO_CACHE1_DMA_CONFIG_1, "PFIFO - Cache1 DMA Config1"},
    { NV3_PFIFO_CACHE1_DMA_CONFIG_2, "PFIFO - Cache1 DMA Config2"},
    { NV3_PFIFO_CACHE1_DMA_CONFIG_3, "PFIFO - Cache1 DMA Config3"},
    { NV3_PFIFO_CACHE1_DMA_STATUS, "PFIFO - Cache1 DMA Status"},
    { NV3_PFIFO_CACHE1_DMA_TLB_PT_BASE, "PFIFO - Cache1 DMA Translation Lookaside Buffer - Pagetable Base"},
    { NV3_PFIFO_CACHE1_DMA_TLB_PTE, "PFIFO - Cache1 DMA Status"},
    { NV3_PFIFO_CACHE1_DMA_TLB_TAG, "PFIFO - Cache1 DMA Status"},
    //Runout
    { NV3_PFIFO_RUNOUT_GET, "PFIFO Runout Get Address [8:3 if 512b, otherwise 12:3]"},
    { NV3_PFIFO_RUNOUT_PUT, "PFIFO Runout Put Address [8:3 if 512b, otherwise 12:3]"},
    { NV_REG_LIST_END, NULL, NULL, NULL}, // sentinel value 
};

// PFIFO init code
void nv3_pfifo_init()
{
    nv_log("NV3: Initialising PFIFO...");

    nv_log("Done!\n");    
}

uint32_t nv3_pfifo_read(uint32_t address) 
{ 
    // before doing anything, check the subsystem enablement state

    if (!(nv3->pmc.enable >> NV3_PMC_ENABLE_PFIFO)
    & NV3_PMC_ENABLE_PFIFO_ENABLED)
    {
        nv_log("NV3: Repressing PFIFO read. The subsystem is disabled according to pmc_enable, returning 0\n");
        return 0x00;
    }

    uint32_t ret = 0x00;

    nv_register_t* reg = nv_get_register(address, pfifo_registers, sizeof(pfifo_registers)/sizeof(pfifo_registers[0]));

    // todo: friendly logging
    
    nv_log("NV3: PFIFO Read from 0x%08x", address);

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
                    ret = nv3->pfifo.cache0_settings.control;
                    break;
                case NV3_PFIFO_CACHE1_PULLER_CONTROL:
                    ret = nv3->pfifo.cache1_settings.control;
                    break;
                case NV3_PFIFO_CACHE0_PULLER_CTX_IS_DIRTY:
                    ret = nv3->pfifo.cache0_settings.context_is_dirty;
                    break;
                case NV3_PFIFO_CACHE1_PULLER_CTX_IS_DIRTY:
                    ret = nv3->pfifo.cache1_settings.context_is_dirty;
                    break;
                case NV3_PFIFO_CACHE0_PUSH_ACCESS:
                    ret = nv3->pfifo.cache0_settings.access_enabled;
                    break;
                case NV3_PFIFO_CACHE1_PUSH_ACCESS:
                    ret = nv3->pfifo.cache1_settings.access_enabled;
                    break; 
                case NV3_PFIFO_CACHE0_PUSH_CHANNEL_ID:
                    ret = nv3->pfifo.cache0_settings.channel;
                    break;
                case NV3_PFIFO_CACHE1_PUSH_CHANNEL_ID:
                    ret = nv3->pfifo.cache1_settings.channel;
                    break;
                case NV3_PFIFO_CACHE0_STATUS:
                    uint32_t ret = 0x00;
                    
                    // CACHE0 has only one entry so it can only ever be empty or full

                    if (nv3->pfifo.cache0_settings.put_address == nv3->pfifo.cache1_settings.get_address)
                        ret |= 1 << NV3_PFIFO_CACHE0_STATUS_EMPTY;
                    else
                        ret |= 1 << NV3_PFIFO_CACHE0_STATUS_FULL;
    
                    break;
                case NV3_PFIFO_CACHE1_STATUS:
                    if (nv3->pfifo.cache1_settings.put_address == nv3->pfifo.cache1_settings.get_address)
                        ret |= 1 << NV3_PFIFO_CACHE1_STATUS_EMPTY;

                    // Check if Cache1 (0x7C bytes in size depending on gpu?) is full
                    // Based on how the drivers do it
                    if (!nv3_pfifo_cache1_is_free())
                        ret |= 1 << NV3_PFIFO_CACHE1_STATUS_FULL;
                    
                    if (nv3->pfifo.runout_put == nv3->pfifo.runout_get)
                        ret |= 1 << NV3_PFIFO_CACHE1_STATUS_RANOUT;

                    ret = nv3->pfifo.cache1_settings.status; 
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
                    ret = nv3->pfifo.cache1_settings.dma_length;
                    break;
                case NV3_PFIFO_CACHE1_DMA_CONFIG_2:
                    ret = nv3->pfifo.cache1_settings.dma_address;
                    break;
                case NV3_PFIFO_CACHE1_DMA_CONFIG_3:
                    ret = nv3->pfifo.cache1_settings.dma_target_node;
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
                case NV3_PFIFO_RUNOUT_GET:
                    ret = nv3->pfifo.runout_get;
                    break;
                case NV3_PFIFO_RUNOUT_PUT:
                    ret = nv3->pfifo.runout_put;
                    break;
                
            }
        }

        if (reg->friendly_name)
            nv_log(": %s\n", reg->friendly_name);
        else   
            nv_log("\n");
    }
    else
    {
        nv_log(": Unknown register read (address=0x%08x), returning 0x00\n", address);
    }

    return ret; 
}

void nv3_pfifo_write(uint32_t address, uint32_t value) 
{
    // before doing anything, check the subsystem enablement

    if (!(nv3->pmc.enable >> NV3_PMC_ENABLE_PFIFO)
    & NV3_PMC_ENABLE_PFIFO_ENABLED)
    {
        nv_log("NV3: Repressing PFIFO write. The subsystem is disabled according to pmc_enable\n");
        return;
    }

    nv_register_t* reg = nv_get_register(address, pfifo_registers, sizeof(pfifo_registers)/sizeof(pfifo_registers[0]));

    nv_log("NV3: PFIFO Write 0x%08x -> 0x%08x\n", value, address);

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
                // Interrupt state:
                // Bit 0 - Cache Error
                // Bit 4 - RAMRO Triggered
                // Bit 8 - RAMRO Overflow (too many invalid dma objects)
                // Bit 12 - DMA Pusher 
                // Bit 16 - DMA Page Table Entry (pagefault?)
                case NV3_PFIFO_INTR:
                    nv3->pfifo.interrupt_status &= ~value;
                    nv3_pmc_clear_interrupts();
                    break;
                case NV3_PFIFO_INTR_EN:
                    nv3->pbus.interrupt_enable = value & 0x00001111;
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

                    nv_log("NV3: RAMHT Reconfiguration\n"
                    "Base Address in RAMIN: %d\n"
                    "Size: 0x%08x bytes\n", ((nv3->pfifo.ramht_config >> NV3_PFIFO_CONFIG_RAMHT_BASE_ADDRESS) & 0x0F) << 12, new_size_ramht); 
#endif
                    break;
                case NV3_PFIFO_CONFIG_RAMFC:
                    nv3->pfifo.ramfc_config = value;

                    nv_log("NV3: RAMFC Reconfiguration\n"
                    "Base Address in RAMIN: %d\n", ((nv3->pfifo.ramfc_config >> NV3_PFIFO_CONFIG_RAMFC_BASE_ADDRESS) & 0x7F) << 9); 
                    break;
                case NV3_PFIFO_CONFIG_RAMRO:
                    nv3->pfifo.ramro_config = value;

                    uint32_t new_size_ramro = ((value >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

                    if (new_size_ramro == 0)
                        new_size_ramro = 0x200;
                    else if (new_size_ramro == 1)
                        new_size_ramro = 0x2000;
                    
                    nv_log("NV3: RAMRO Reconfiguration\n"
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
                    nv3->pfifo.cache0_settings.control = value; // 8bits meaningful
                    break;
                case NV3_PFIFO_CACHE1_PULLER_CONTROL:
                    nv3->pfifo.cache1_settings.control = value; // 8bits meaningful
                    break;
                case NV3_PFIFO_CACHE0_PULLER_CTX_IS_DIRTY:
                    nv3->pfifo.cache0_settings.context_is_dirty = value;
                    break;
                case NV3_PFIFO_CACHE1_PULLER_CTX_IS_DIRTY:
                    nv3->pfifo.cache1_settings.context_is_dirty = value;
                    break;
                case NV3_PFIFO_CACHE0_PUSH_ACCESS:
                    nv3->pfifo.cache0_settings.access_enabled = value;
                    break;
                case NV3_PFIFO_CACHE1_PUSH_ACCESS:
                    nv3->pfifo.cache1_settings.access_enabled = value;
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
                case NV3_PFIFO_CACHE1_DMA_CONFIG_3:
                    nv3->pfifo.cache1_settings.dma_target_node = value;
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
                    uint32_t size = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

                    if (size == 0) //512b
                        nv3->pfifo.runout_get = ((value & 0x3F) << 3);
                    else 
                        nv3->pfifo.runout_get = ((value & 0x3FF) << 3);
                    break;
                case NV3_PFIFO_RUNOUT_PUT:
                    uint32_t size = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

                    if (size == 0) //512b
                        nv3->pfifo.runout_put = ((value & 0x3F) << 3);
                    else 
                        nv3->pfifo.runout_put = ((value & 0x3FF) << 3);
                    break;
            }
        }
    }

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
        // NT4 drivers v1.29
        mask >>= 1; 
        val ^= mask;
        
    }

    return val;
}

void nv3_pfifo_cache0_push()
{

}

void nv3_pfifo_cache0_pull()
{
    // Do nothing if PFIFO CACHE0 is disabled
    if (!nv3->pfifo.cache0_settings.puller_control & (1 >> NV3_PFIFO_CACHE0_PULLER_CONTROL_ENABLED))
        return; 

    // Do nothing if there is nothing in cache0 to pull
    if (nv3->pfifo.cache0_settings.put_address == nv3->pfifo.cache0_settings.get_address)
        return;

    // There is only one entry for cache0 
    uint16_t current_channel = nv3->pfifo.cache0_settings.channel;
    uint32_t current_subchannel = nv3->pfifo.cache0_entry.subchannel;
    uint32_t current_name = nv3->pfifo.cache0_entry.data;
    uint32_t current_method = nv3->pfifo.cache0_entry.method;
    
    // i.e. there is no method in cache0, so we have to find the object.
    if (!current_method)
    {
        if (!nv3_ramin_find_object(current_name, 0, current_channel, current_subchannel))
            return; // interrupt was fired, and we went to ramro
    }

    uint32_t current_context = nv3->pfifo.cache0_settings.context[0]; // only 1 entry for CACHE0 so basically ignore the other context entries?

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
    nv_log("NV3: ***** SUBMITTING GRAPHICS COMMANDS CURRENTLY UNIMPLEMENTED - CACHE0 PULLED ****** Contextual information below\n");
            
    nv3_debug_ramin_print_context_info(current_name, *(nv3_ramin_context_t*)current_context);
    #endif

}

void nv3_pfifo_cache1_push()
{

}

void nv3_pfifo_cache1_pull()
{
    // Do nothing if PFIFO CACHE1 is disabled
    if (!nv3->pfifo.cache1_settings.puller_control & (1 >> NV3_PFIFO_CACHE1_PULLER_CONTROL_ENABLED))
        return; 

    // Do nothing if there is nothing in cache1 to pull
    if (nv3->pfifo.cache1_settings.put_address == nv3->pfifo.cache1_settings.get_address)
        return;

    // There is only one entry for cache0 
    uint32_t get_address = nv3->pfifo.cache1_settings.get_address >> 2; // 32 bit aligned probably

    uint16_t current_channel = nv3->pfifo.cache1_settings.channel;
    uint32_t current_subchannel = nv3->pfifo.cache1_entries[get_address].subchannel;
    uint32_t current_name = nv3->pfifo.cache1_entries[get_address].data;
    uint32_t current_method = nv3->pfifo.cache1_entries[get_address].method;
  
    // i.e. there is no method in cache0, so we have to find the object.
    if (!current_method)
    {
        if (!nv3_ramin_find_object(current_name, 0, current_channel, current_subchannel))
            return; // interrupt was fired, and we went to ramro
    }

    uint32_t current_context = nv3->pfifo.cache0_settings.context[0]; // only 1 entry for CACHE0 so basically ignore the other context entries?

    // Tell the CPU if we found a software method
    if (current_context & 0x800000)
    {
        nv3->pfifo.cache0_settings.puller_control |= NV3_PFIFO_CACHE0_PULLER_CONTROL_SOFTWARE_METHOD;
        nv3->pfifo.cache0_settings.puller_control &= ~NV3_PFIFO_CACHE0_PULLER_CONTROL_ENABLED;
        nv3_pfifo_interrupt(NV3_PFIFO_INTR_CACHE_ERROR, true);
    }

    // start by incrementing
    uint32_t next_get_address = nv3_pfifo_cache1_gray2normal(get_address) + 1;
    
    if (nv3->nvbase.gpu_revision >= NV3_BOOT_REG_REV_C00) // RIVA 128ZX#
        next_get_address &= NV3_PFIFO_CACHE1_SIZE_REV_C;
    else 
        next_get_address &= NV3_PFIFO_CACHE1_SIZE_REV_AB;

    // Is this needed?
    nv3->pfifo.cache0_settings.get_address = nv3_pfifo_cache1_normal2gray(next_get_address) << 2;

    #ifndef RELEASE_BUILD
    nv_log("NV3: ***** SUBMITTING GRAPHICS COMMANDS CURRENTLY UNIMPLEMENTED - CACHE1 PULLED ****** Contextual information below\n");
            
    nv3_debug_ramin_print_context_info(current_name, *(nv3_ramin_context_t*)current_context);
    #endif

    //Todo: finish it
}

bool nv3_pfifo_cache1_is_free()
{
    // convert to gray code
    uint32_t real_get_address = nv3_pfifo_cache1_normal2gray(nv3->pfifo.cache1_settings.get_address);
    uint32_t real_put_address = nv3_pfifo_cache1_normal2gray(nv3->pfifo.cache1_settings.put_address);
    
    // There is no hope of being able to understand it. Nobody can understand
    return (real_get_address - real_put_address - 4) & 0x7C; // there are 64 entries what
}