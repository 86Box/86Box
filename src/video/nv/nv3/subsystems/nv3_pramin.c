/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PRAMIN - Basically, this is how we know what to render.
 *          Has a giant hashtable of all the submitted DMA objects using a pseudo-C++ class system
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

// Functions only used in this translation unit
#ifndef RELEASE_BUILD
void nv3_debug_ramin_print_context_info(uint32_t name, nv3_ramin_context_t context);
#endif

// Notes for all of these functions:
// Structures in RAMIN are stored from the bottom of vram up in reverse order
// this can be explained without bitwise math like so:
// real VRAM address = VRAM_size - (ramin_address - (ramin_address % reversal_unit_size)) - reversal_unit_size + (ramin_address % reversal_unit_size) 
// reversal unit size in this case is 16 bytes, vram size is 2-8mb (but 8mb is zx/nv3t only and 2mb...i haven't found a 22mb card)

/* Addresses are offsets into RAMIN, either from the BAR1 window (whose bus address is masked to
   the 4MB window here) or from the chip itself; RAMIN offset x is VRAM (x ^ (size - 16)). Masking
   with the VRAM size alone kept bit 22 of the window's 0xC00000 on 8MB cards (RIVA 128 ZX), so
   the driver's RAMHT went 4MB away from where the chip looked it up. */
// Read 8-bit ramin
uint8_t nv3_ramin_read8(uint32_t addr, UNUSED(void* priv))
{
    if (!nv3) return 0x00;

    addr &= (NV3_LFB_MAPPING_SIZE - 1) & (nv3->nvbase.svga.vram_max - 1);

    uint32_t ramin_addr = (addr ^ (nv3->nvbase.svga.vram_max - 0x10));
    uint8_t val = nv3->nvbase.svga.vram[ramin_addr];
    
    nv_log_verbose_only("Read word from PRAMIN 0x%08x <- 0x%08x (raw address=0x%08x)\n", ramin_addr, val, addr);

    return val;
}

// Read 16-bit ramin
uint16_t nv3_ramin_read16(uint32_t addr, UNUSED(void* priv))
{
    if (!nv3) return 0x00;

    addr &= (NV3_LFB_MAPPING_SIZE - 1) & (nv3->nvbase.svga.vram_max - 1);

    uint16_t* vram_16bit = (uint16_t*)nv3->nvbase.svga.vram;
    uint32_t ramin_addr = (addr ^ (nv3->nvbase.svga.vram_max - 0x10)) >> 1;

    uint16_t val = vram_16bit[ramin_addr];

    nv_log_verbose_only("Read word from PRAMIN 0x%08x <- 0x%08x (raw address=0x%08x)\n", ramin_addr, val, addr);

    return val;
}

// Read 32-bit ramin
uint32_t nv3_ramin_read32(uint32_t addr, UNUSED(void* priv))
{
    if (!nv3) return 0x00;

    addr &= (NV3_LFB_MAPPING_SIZE - 1) & (nv3->nvbase.svga.vram_max - 1);

    uint32_t* vram_32bit = (uint32_t*)nv3->nvbase.svga.vram;
    uint32_t ramin_addr = (addr ^ (nv3->nvbase.svga.vram_max - 0x10)) >> 2;

    uint32_t val = vram_32bit[ramin_addr];
    nv_log_verbose_only("Read dword from PRAMIN 0x%08x <- 0x%08x (raw address=0x%08x)\n", ramin_addr, val, addr);

    return val;
}

// RAMIN write functions 

// Write 8-bit ramin
void nv3_ramin_write8(uint32_t addr, uint8_t val, UNUSED(void* priv))
{
    if (!nv3) return;

    addr &= (NV3_LFB_MAPPING_SIZE - 1) & (nv3->nvbase.svga.vram_max - 1);

    uint32_t ramin_addr = (addr ^ (nv3->nvbase.svga.vram_max - 0x10));
    nv3_watch(ramin_addr, val, 108);
    nv3->nvbase.svga.vram[ramin_addr] = val;

    nv_log_verbose_only("Write byte to PRAMIN addr=0x%08x val=0x%02x (raw address=0x%08x)\n", ramin_addr, val, addr);
}

// Write 16-bit ramin
void nv3_ramin_write16(uint32_t addr, uint16_t val, UNUSED(void* priv))
{
    if (!nv3) return;

    addr &= (NV3_LFB_MAPPING_SIZE - 1) & (nv3->nvbase.svga.vram_max - 1);

    svga_t* svga = &nv3->nvbase.svga;
    uint16_t* vram_16bit = (uint16_t*)svga->vram;

    uint32_t ramin_addr = (addr ^ (nv3->nvbase.svga.vram_max - 0x10)) >> 1;
    nv3_watch(ramin_addr << 1, val, 116);
    vram_16bit[ramin_addr] = val;

    nv_log_verbose_only("Write word to PRAMIN addr=0x%08x val=0x%04x (raw address=0x%08x)\n", ramin_addr, val, addr);
}

// Write 32-bit ramin
void nv3_ramin_write32(uint32_t addr, uint32_t val, UNUSED(void* priv))
{
    if (!nv3) return;

    addr &= (NV3_LFB_MAPPING_SIZE - 1) & (nv3->nvbase.svga.vram_max - 1);

    svga_t* svga = &nv3->nvbase.svga;
    uint32_t* vram_32bit = (uint32_t*)svga->vram;

    uint32_t ramin_addr = (addr ^ (nv3->nvbase.svga.vram_max - 0x10)) >> 2;
    nv3_watch(ramin_addr << 2, val, 132);
    vram_32bit[ramin_addr] = val;

    nv_log_verbose_only("Write dword to PRAMIN addr=0x%08x val=0x%08x (raw address=0x%08x)\n", ramin_addr, val, addr);

}

void nv3_pfifo_interrupt(uint32_t id, bool fire_now)
{
    nv3->pfifo.intr |= (1 << id);
    nv3_pmc_handle_interrupts(fire_now);
}

// THIS IS THE MOST IMPORTANT FUNCTION!
bool nv3_ramin_find_object(uint32_t name, uint32_t cache_num, uint8_t channel, uint8_t subchannel)
{  
    // 4KB = 2, 8KB = 4, 16KB = 8, 32KB = 16. Newer GPUs may have more
    uint32_t bucket_entries = nv3->pfifo.ramht_size >> 11;

    // stored like this to optimise searches probably
    uint32_t ramht_cur_address = nv3->pfifo.ramht_location + (nv3_ramht_hash(name, channel) * bucket_entries << 3); 

    nv_log_verbose_only("Beginning search for graphics object at RAMHT base=0x%04x, name=0x%08x, Cache%d, channel=%d.%d)\n",
        ramht_cur_address, name, cache_num, channel, subchannel);

    bool found_object = false;
    
    // set up some variables
    uint32_t found_obj_name = 0x00;
    nv3_ramin_context_t obj_context_struct = {0};

    for (uint32_t bucket_entry = 0; bucket_entry < bucket_entries; bucket_entry++)
    {
        found_obj_name = nv3_ramin_read32(ramht_cur_address, NULL);
        uint32_t obj_context = nv3_ramin_read32(ramht_cur_address + 4, NULL);
        ramht_cur_address += 0x08;
        obj_context_struct = *(nv3_ramin_context_t*)&obj_context;

        // see if the object is in the right channel
        if (found_obj_name == name
            && obj_context_struct.channel == channel)
        {
            found_object = true;
                
            // This bit fucked me for an extremely long time
            if (!cache_num)
                nv3->pfifo.cache0_settings.context[0] = obj_context;
            else {
                extern uint32_t nv3_swm_trace_left;
                if (nv3_swm_trace_left && (nv3->pfifo.cache1_settings.context[subchannel] != obj_context))
                    always_log("nv3: ramht bind subch %d handle %08x ctx %08x -> %08x (entry %04x)%c", subchannel, name,
                               nv3->pfifo.cache1_settings.context[subchannel], obj_context, ramht_cur_address - 8, 10);
                nv3->pfifo.cache1_settings.context[subchannel] = obj_context;
            }
    
            break;
        }
    }

    if (!found_object)
    {
        {
            extern uint32_t nv3_swm_trace_left, nv3_mmio_trace_left;
            if (nv3_swm_trace_left) {
                always_log("nv3: ramht miss cache%d chan %d subch %d handle %08x%c", cache_num, channel, subchannel, name, 10);
                nv3_mmio_trace_left = 200;
            }
        }
        if (!cache_num)
        {
            nv3->pfifo.debug_0 |= (1 << NV3_PFIFO_CACHE0_ERROR_PENDING);
            nv3->pfifo.cache0_settings.pull0 |= (1 << NV3_PFIFO_CACHE0_PULL0_HASH_FAILURE);
            //It turns itself off on failure, the drivers turn it back on
            nv3->pfifo.cache0_settings.pull0 &= ~(1 << NV3_PFIFO_CACHE0_PULL0_ENABLED);
        } 
        else 
        {
            nv3->pfifo.debug_0 |= (1 << NV3_PFIFO_CACHE1_ERROR_PENDING);
            nv3->pfifo.cache1_settings.pull0 |= (1 << NV3_PFIFO_CACHE1_PULL0_HASH_FAILURE);
            //It turns itself off on failure, the drivers turn it back on
            nv3->pfifo.cache1_settings.pull0 &= ~(1 << NV3_PFIFO_CACHE1_PULL0_ENABLED);
        }

        nv3_pfifo_interrupt(NV3_PFIFO_INTR_CACHE_ERROR, true);

        return false;
    }

    // So we did find an object.
    // Now try to read some of this...
            
    // Class ID is 5 bits in all other parts of the gpu but 7 bits here. A move in a direction that didn't pan out?
    // Represented as 0x40-0x5f? Some other meaning

    // Perform more validation 

    /* Not errors: PGRAPH uses only the class's low five bits (a software object's class is the
       resman's business), and the channel is the 7-bit CHID the entry was just matched on --
       the resman uses up to 32 channels. Both used to be fatal(). */
    if (obj_context_struct.class_id < NV3_PFIFO_FIRST_VALID_GRAPHICS_OBJECT_ID
        || obj_context_struct.class_id > NV3_PFIFO_LAST_VALID_GRAPHICS_OBJECT_ID)
        nv_warning("NV3: object %08x has class %02x outside 0x40-0x5F (PGRAPH sees %02x)\n", name, obj_context_struct.class_id,
                   obj_context_struct.class_id & 0x1F);

    // Illegal accesses sent to RAMRO, so ignore here
    // TODO: SEND THESE TO RAMRO!!!!!

    #ifndef RELEASE_BUILD
    nv3_debug_ramin_print_context_info(name, obj_context_struct);
    #endif

    // By definition we can't have a cache error by here so take it off
    if (!cache_num)
        nv3->pfifo.cache0_settings.pull0 &= ~(1 << NV3_PFIFO_CACHE0_PULL0_HASH_FAILURE);
    else
        nv3->pfifo.cache1_settings.pull0 &= ~(1 << NV3_PFIFO_CACHE1_PULL0_HASH_FAILURE);

    /* A software object (context bit 23 clear) is the puller's business: its
       methods, this bind included, stop CACHE1 with a software-method error */

    // done
    return true; 
    
}


/* This implements the hash that all the objects are stored within.
It is used to get the offset within RAMHT of a graphics object.
 */

uint32_t nv3_ramht_hash(uint32_t name, uint32_t channel)
{
    // the official nvidia hash algorithm, tweaked for readability
    uint32_t hash = ((name ^ (name >> 8) ^ (name >> 16) ^ (name >> 24)) & 0xFF) ^ (channel & NV3_DMA_CHANNELS_TOTAL); 
    // is this the right endianness?
    nv_log_verbose_only("Generated RAMHT hash 0x%04x (RAMHT slot=0x%04x (from name 0x%08x for DMA channel 0x%04x)\n)\n", hash, (hash/8), name, channel);
    return hash;
}


// Prints out some informaiton about the object
void nv3_debug_ramin_print_context_info(uint32_t name, nv3_ramin_context_t context)
{
    #ifndef RELEASE_BUILD
    nv_log_verbose_only("Found object:\n");
    nv_log_verbose_only("Param: 0x%04x\n", name);

    nv_log_verbose_only("Context:\n");
    nv_log_verbose_only("DMA Channel %d (0-7 valid)\n", context.channel);
    nv_log_verbose_only("Class ID: 0x%04x (%s)\n", context.class_id & 0x1F, nv3_class_names[context.class_id & 0x1F]);
    nv_log_verbose_only("Render Engine %d (0=Software, also DMA? 1=Accelerated Renderer)\n", context.is_rendering);
    nv_log_verbose_only("PRAMIN Offset 0x%08x\n", context.ramin_offset << 4);
    #endif
}
