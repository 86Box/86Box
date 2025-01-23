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
    { NV3_PFIFO_CONFIG_RAMFC, "PFIFO - RAMIN RAMFC Config", NULL, NULL },
    { NV3_PFIFO_CONFIG_RAMHT, "PFIFO - RAMIN RAMHT Config", NULL, NULL },
    { NV3_PFIFO_CONFIG_RAMRO, "PFIFO - RAMIN RAMRO Config", NULL, NULL },
    { NV3_PFIFO_CACHE0_PULLER_CONTROL, "PFIFO - Cache0 Puller State0", NULL, NULL},
    { NV3_PFIFO_CACHE0_PULLER_STATE1, "PFIFO - Cache0 Puller State1 (Is context clean?)", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULLER_CONTROL, "PFIFO - Cache1 Puller State0", NULL, NULL},
    { NV3_PFIFO_CACHE1_PULLER_STATE1, "PFIFO - Cache1 Puller State1 (Is context clean?)", NULL, NULL},

    { NV3_PFIFO_CACHE0_STATUS, "PFIFO - Cache0 Status", NULL, NULL},
    { NV3_PFIFO_CACHE1_STATUS, "PFIFO - Cache1 Status", NULL, NULL}, 
    { NV3_PFIFO_CACHE0_GET, "PFIFO - Cache0 Get MUST TRIGGER DMA NOW TO OBTAIN ENTRY", NULL, NULL },
    { NV3_PFIFO_CACHE1_GET, "PFIFO - Cache1 Get MUST TRIGGER DMA NOW TO OBTAIN ENTRY", NULL, NULL },
    { NV3_PFIFO_CACHE0_PUT, "PFIFO - Cache0 Put MUST TRIGGER DMA NOW TO INSERT ENTRY", NULL, NULL },
    { NV3_PFIFO_CACHE1_PUT, "PFIFO - Cache1 Put MUST TRIGGER DMA NOW TO INSERT ENTRY", NULL, NULL },
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
                // Debug
                case NV3_PFIFO_DEBUG_0:
                    ret = nv3->pfifo.debug_0;
                    break;
                // These may need to become functions.
                case NV3_PFIFO_CONFIG_RAMFC:
                    ret = nv3->pfifo.ramfc_config;
                    break;
                case NV3_PFIFO_CONFIG_RAMHT:
                    ret = nv3->pfifo.ramht_config;
                    break;
                case NV3_PFIFO_CONFIG_RAMRO:
                    ret = nv3->pfifo.ramro_config;
                    break;
                case NV3_PFIFO_CACHE0_GET:
                    //wa
                    break;
                // Reassignment
                case NV3_PFIFO_CACHE_REASSIGNMENT:
                    ret = nv3->pfifo.cache_reassignment & 0x01; //1bit meaningful
                    break;
                // Control
                case NV3_PFIFO_CACHE0_PULLER_CONTROL:
                    ret = nv3->pfifo.cache0_settings.control & 0xFF; // 8bits meaningful
                    break;
                case NV3_PFIFO_CACHE1_PULLER_CONTROL:
                    ret = nv3->pfifo.cache1_settings.control & 0xFF; // only 8bits are meaningful
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

                    uint32_t new_size_ramro = ((value >> 16) & 0x01);

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