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
#include <86box/86box.h>
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

// i believe the main loop is to walk the hashtable in RAMIN (last 0.5 MB of VRAM), 
// find the objects that were submitted from DMA 
// (going from software -> nvidia d3d / ogl implementation -> resource manager client -> nvapi -> nvrm -> GPU PFIFO -> GPU PBUS -> GPU PFB RAMIN -> PGRAPH) 
// and then rendering each of those using PGRAPH

// Notes for all of these functions:
// Structures in RAMIN are stored from the bottom of vram up in reverse order
// this can be explained without bitwise math like so:
// real VRAM address = VRAM_size - (ramin_address - (ramin_address % reversal_unit_size)) - reversal_unit_size + (ramin_address % reversal_unit_size) 
// reversal unit size in this case is 16 bytes, vram size is 2-8mb (but 8mb is zx/nv3t only and 2mb...i haven't found a 22mb card)

// Read 8-bit ramin
uint8_t nv3_ramin_read8(uint32_t addr, void* priv)
{
    if (!nv3) return 0x00;

    addr &= (nv3->nvbase.svga.vram_max - 1);
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max- 0x10);

    uint32_t val = 0x00;

    if (!nv3_ramin_arbitrate_read(addr, &val)) // Oh well
    {
        val = (uint8_t)nv3->nvbase.svga.vram[addr];
        nv_log("Read byte from PRAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);
    }

    return (uint8_t)val;
}

// Read 16-bit ramin
uint16_t nv3_ramin_read16(uint32_t addr, void* priv)
{
    if (!nv3) return 0x00;

    addr &= (nv3->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv3->nvbase.svga;
    uint16_t* vram_16bit = (uint16_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max - 0x10);
    addr >>= 1; // what

    uint32_t val = 0x00;

    if (!nv3_ramin_arbitrate_read(addr, &val))
    {
        val = (uint16_t)vram_16bit[addr];
        nv_log("Read word from PRAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);
    }

    return val;
}

// Read 32-bit ramin
uint32_t nv3_ramin_read32(uint32_t addr, void* priv)
{
    if (!nv3) return 0x00;

    addr &= (nv3->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv3->nvbase.svga;
    uint32_t* vram_32bit = (uint32_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max - 0x10);
    addr >>= 2; // what

    uint32_t val = 0x00;

    if (!nv3_ramin_arbitrate_read(addr, &val))
    {
        val = vram_32bit[addr];

        nv_log("Read dword from PRAMIN 0x%08x <- 0x%08x (raw address=0x%08x)\n", val, addr, raw_addr);
    }

    return val;
}

// Write 8-bit ramin
void nv3_ramin_write8(uint32_t addr, uint8_t val, void* priv)
{
    if (!nv3) return;

    addr &= (nv3->nvbase.svga.vram_max - 1);
    uint32_t raw_addr = addr; // saved after and

    // Structures in RAMIN are stored from the bottom of vram up in reverse order
    // this can be explained without bitwise math like so:
    // real VRAM address = VRAM_size - (ramin_address - (ramin_address % reversal_unit_size)) - reversal_unit_size + (ramin_address % reversal_unit_size) 
    // reversal unit size in this case is 16 bytes, vram size is 2-8mb (but 8mb is zx/nv3t only and 2mb...i haven't found a 22mb card)
    addr ^= (nv3->nvbase.svga.vram_max - 0x10);

    uint32_t val32 = (uint32_t)val;

    if (!nv3_ramin_arbitrate_write(addr, val32))
    {
        nv3->nvbase.svga.vram[addr] = val;
        nv_log("Write byte to PRAMIN addr=0x%08x val=0x%02x (raw address=0x%08x)\n", addr, val, raw_addr);
    }


}

// Write 16-bit ramin
void nv3_ramin_write16(uint32_t addr, uint16_t val, void* priv)
{
    if (!nv3) return;

    addr &= (nv3->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv3->nvbase.svga;
    uint16_t* vram_16bit = (uint16_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max - 0x10);
    addr >>= 1; // what

    uint32_t val32 = (uint32_t)val;

    if (!nv3_ramin_arbitrate_write(addr, val32))
    {
        vram_16bit[addr] = val;
        nv_log("Write word to PRAMIN addr=0x%08x val=0x%04x (raw address=0x%08x)\n", addr, val, raw_addr);
    }


}

// Write 32-bit ramin
void nv3_ramin_write32(uint32_t addr, uint32_t val, void* priv)
{
    if (!nv3) return;

    addr &= (nv3->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv3->nvbase.svga;
    uint32_t* vram_32bit = (uint32_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max - 0x10);
    addr >>= 2; // what

    if (!nv3_ramin_arbitrate_write(addr, val))
    {
        vram_32bit[addr] = val;
        nv_log("Write dword to PRAMIN addr=0x%08x val=0x%08x (raw address=0x%08x)\n", addr, val, raw_addr);
    }

}

void nv3_pfifo_interrupt(uint32_t id, bool fire_now)
{
    nv3->pfifo.interrupt_status |= (1 << id);
    nv3_pmc_handle_interrupts(fire_now);
}

/* 
RAMIN access arbitration functions
Arbitrates reads and writes to RAMFC (unused dma context storage), RAMRO (invalid object submission location), RAMHT (hashtable for graphics objectstorage) (RAMAU?) 
and generic RAMIN

Takes a pointer to a result integer. This is because we need to check its result in our normal write function.
Returns true if a valid "non-generic" address was found (e.g. RAMFC/RAMRO/RAMHT). False if the specified address is a generic RAMIN address
*/
bool nv3_ramin_arbitrate_read(uint32_t address, uint32_t* value)
{
    if (!nv3) return 0x00;

    uint32_t ramht_size = ((nv3->pfifo.ramht_config >> NV3_PFIFO_CONFIG_RAMHT_SIZE) & 0x03);
    uint32_t ramro_size = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

    // Get the addresses of RAMHT, RAMFC, RAMRO
    // They must be within first 64KB of PRAMIN!
    uint32_t ramht_start = ((nv3->pfifo.ramht_config >> NV3_PFIFO_CONFIG_RAMHT_BASE_ADDRESS) & 0x0F) << 12;  // Must be 0x1000 aligned
    uint32_t ramfc_start = ((nv3->pfifo.ramfc_config >> NV3_PFIFO_CONFIG_RAMFC_BASE_ADDRESS) & 0x7F) << 9;   // Must be 0x200 aligned
    uint32_t ramro_start = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_BASE_ADDRESS) & 0x7F) << 9;   // Must be 0x200 aligned

    // Calculate the RAMHT and RAMRO end points.
    // (RAMFC is always 0x1000 bytes on NV3.)
    uint32_t ramht_end = ramht_start;
    uint32_t ramfc_end = ramfc_start + 0x1000;
    uint32_t ramro_end = ramro_start;

    switch (ramht_size)
    {
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_4K:
            ramht_end = ramht_start + NV3_RAMIN_RAMHT_SIZE_0;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_8K:
            ramht_end = ramht_start + NV3_RAMIN_RAMHT_SIZE_1;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_16K:
            ramht_end = ramht_start + NV3_RAMIN_RAMHT_SIZE_2;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_32K:
            ramht_end = ramht_start + NV3_RAMIN_RAMHT_SIZE_3;
            break;
    }

    switch (ramro_size)
    {
        case NV3_PFIFO_CONFIG_RAMRO_SIZE_512B:
            ramro_end = ramro_start + NV3_RAMIN_RAMRO_SIZE_0;
            break;
        case NV3_PFIFO_CONFIG_RAMRO_SIZE_8K:
            ramro_end = ramro_start + NV3_RAMIN_RAMRO_SIZE_1;
            break;
    }

    if (address >= ramht_start 
    && address <= ramht_end)
    {
        *value = nv3_ramht_read(address);
        return true;
    }
    else if (address >= ramfc_start 
    && address <= ramfc_end)
    {
        *value = nv3_ramfc_read(address);
        return true;
    }
    else if (address >= ramro_start 
    && address <= ramro_end)
    {
        *value = nv3_ramro_read(address);
        return true;
    }
 
    /* temp */
    return false;
}

bool nv3_ramin_arbitrate_write(uint32_t address, uint32_t value) 
{
    if (!nv3) return 0x00;

    uint32_t ramht_size = ((nv3->pfifo.ramht_config >> NV3_PFIFO_CONFIG_RAMHT_SIZE) & 0x03);
    uint32_t ramro_size = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

    // Get the addresses of RAMHT, RAMFC, RAMRO
    // They must be within first 64KB of PRAMIN!
    uint32_t ramht_start = ((nv3->pfifo.ramht_config >> NV3_PFIFO_CONFIG_RAMHT_BASE_ADDRESS) & 0x0F) << 12;  // Must be 0x1000 aligned
    uint32_t ramfc_start = ((nv3->pfifo.ramfc_config >> NV3_PFIFO_CONFIG_RAMFC_BASE_ADDRESS) & 0x7F) << 9;   // Must be 0x200 aligned
    uint32_t ramro_start = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_BASE_ADDRESS) & 0x7F) << 9;   // Must be 0x200 aligned

    // Calculate the RAMHT and RAMRO end points.
    // (RAMFC is always 0x1000 bytes on NV3.)
    uint32_t ramht_end = ramht_start;
    uint32_t ramfc_end = ramfc_start + 0x1000;
    uint32_t ramro_end = ramro_start;

    switch (ramht_size)
    {
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_4K:
            ramht_end = ramht_start + NV3_RAMIN_RAMHT_SIZE_0;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_8K:
            ramht_end = ramht_start + NV3_RAMIN_RAMHT_SIZE_1;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_16K:
            ramht_end = ramht_start + NV3_RAMIN_RAMHT_SIZE_2;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_32K:
            ramht_end = ramht_start + NV3_RAMIN_RAMHT_SIZE_3;
            break;
    }

    switch (ramro_size)
    {
        case NV3_PFIFO_CONFIG_RAMRO_SIZE_512B:
            ramro_end = ramro_start + NV3_RAMIN_RAMRO_SIZE_0;
            break;
        case NV3_PFIFO_CONFIG_RAMRO_SIZE_8K:
            ramro_end = ramro_start + NV3_RAMIN_RAMRO_SIZE_1;
            break;
    }

    // send the addresses to the right part
    if (address >= ramht_start 
    && address <= ramht_end)
    {
        nv3_ramht_write(address, value);
        return true;
    }
    else if (address >= ramfc_start 
    && address <= ramfc_end)
    {
        nv3_ramfc_write(address, value);
        return true;
    }
    else if (address >= ramro_start 
    && address <= ramro_end)
    {
        nv3_ramro_write(address, value);
        return true;
    }

    return false;
}

// THIS IS THE MOST IMPORTANT FUNCTION!
bool nv3_ramin_find_object(uint32_t name, uint32_t cache_num, uint8_t channel, uint8_t subchannel)
{  
    // TODO: WRITE IT!!!
    // Set the number of entries to search based on the ramht size (2*(size+1))
    // Not a switch statement in case newer gpus have larger ramins

    uint32_t bucket_entries = 2;
    uint8_t ramht_size = (nv3->pfifo.ramht_config >> NV3_PFIFO_CONFIG_RAMHT_SIZE) & 0x03;

    switch (ramht_size)
    {
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_4K:
            // stays as is
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_8K:
            bucket_entries = 4; 
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_16K:
            bucket_entries = 8;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_32K:
            bucket_entries = 16;
            break;
        
    }
    
    // Calculate the address in the hashtable
    uint32_t ramht_base = ((nv3->pfifo.ramht_config >> NV3_PFIFO_CONFIG_RAMHT_BASE_ADDRESS) & 0x0F) << NV3_PFIFO_CONFIG_RAMHT_BASE_ADDRESS;

    // This is certainly wrong. But the objects seem to be written to 4600? So I just multiply it by 80 to multiply the final address by 10.
    // Why does this work?
    uint32_t ramht_cur_address = ramht_base + (nv3_ramht_hash(name, channel) * bucket_entries * 8);

    nv_log("Beginning search for graphics object at RAMHT base=0x%04x, name=0x%08x, Cache%d, channel=%d.%d)\n",
        ramht_cur_address, name, cache_num, channel, subchannel);

    bool found_object = false;
    
    // set up some variables
    uint32_t found_obj_name = 0x00;
    nv3_ramin_context_t obj_context_struct = {0};

    for (uint32_t bucket_entry = 0; bucket_entry < bucket_entries; bucket_entry++)
    {
        found_obj_name = nv3_ramin_read32(ramht_cur_address, NULL);
        ramht_cur_address += 0x04;
        uint32_t obj_context = nv3_ramin_read32(ramht_cur_address, NULL);
        ramht_cur_address += 0x04;
        obj_context_struct = *(nv3_ramin_context_t*)&obj_context;

        // see if the object is in the right channel
        if (found_obj_name == name
            && obj_context_struct.channel == channel)
        {
            found_object = true;
            break;
        }
    }

    if (!found_object)
    {
        nv3->pfifo.debug_0 |= NV3_PFIFO_CACHE0_ERROR_PENDING;

        if (!cache_num)
        {
            nv3->pfifo.cache0_settings.puller_control |= NV3_PFIFO_CACHE0_PULLER_CONTROL_HASH_FAILURE;
            //It turns itself off on failure, the drivers turn it back on
            nv3->pfifo.cache0_settings.puller_control &= ~NV3_PFIFO_CACHE0_PULLER_CONTROL_ENABLED;
        } 
        else 
        {
            nv3->pfifo.cache1_settings.puller_control |= NV3_PFIFO_CACHE1_PULLER_CONTROL_HASH_FAILURE;
            //It turns itself off on failure, the drivers turn it back on
            nv3->pfifo.cache1_settings.puller_control &= ~NV3_PFIFO_CACHE1_PULLER_CONTROL_ENABLED;
        }

        nv3_pfifo_interrupt(NV3_PFIFO_INTR_CACHE_ERROR, true);

        return false;
    }

    // So we did find an object.
    // Now try to read some of this...
            
    // Class ID is 5 bits in all other parts of the gpu but 7 bits here. A move in a direction that didn't pan out?
    // Represented as 0x40-0x5f? Some other meaning

    // Perform more validation 

    if (obj_context_struct.class_id > NV3_PFIFO_FIRST_VALID_GRAPHICS_OBJECT_ID
    || obj_context_struct.class_id < NV3_PFIFO_LAST_VALID_GRAPHICS_OBJECT_ID)
    {
        fatal("NV3: Invalid graphics object class ID name=0x%04x type=%04x, interpreted by pgraph as: %04x (Contact starfrost)", 
            name, obj_context_struct.class_id, obj_context_struct.class_id & 0x1F);
    }   
    else if (obj_context_struct.channel > NV3_DMA_CHANNELS)
        fatal("NV3: Super fucked up graphics object. Contact starfrost with the error string: DMA Channel ID=%d, it should be 0-8", obj_context_struct.channel);
    
    // Illegal accesses sent to RAMRO, so ignore here
    // TODO: SEND THESE TO RAMRO!!!!!

    #ifndef RELEASE_BUILD
    nv3_debug_ramin_print_context_info(name, obj_context_struct);
    #endif

    // By definition we can't have a cache error by here so take it off
    if (!cache_num)
        nv3->pfifo.cache0_settings.puller_control &= ~NV3_PFIFO_CACHE0_PULLER_CONTROL_HASH_FAILURE;
    else
        nv3->pfifo.cache1_settings.puller_control &= ~NV3_PFIFO_CACHE1_PULLER_CONTROL_HASH_FAILURE;

    // Caches store all the subchannels for our current dma channel and basically get stale every context switch
    // Also we have to check that a osftware object didn't end up in here...
    
    bool is_software = false;
    if (!cache_num)
        is_software = (nv3->pfifo.cache0_settings.context[subchannel] & 0x800000);
    else 
        is_software = (nv3->pfifo.cache1_settings.context[subchannel] & 0x800000);

    // This isn't an error but it's sent as an interrupt so the drivers can sync
    if (is_software)
    {  
        // handle it as an error 
        if (!cache_num)
        {
            nv3->pfifo.cache0_settings.puller_control |= NV3_PFIFO_CACHE0_PULLER_CONTROL_SOFTWARE_METHOD;
            nv3->pfifo.cache0_settings.puller_control &= ~NV3_PFIFO_CACHE0_PULLER_CONTROL_ENABLED;
        }
        else   
        {
            nv3->pfifo.cache1_settings.puller_control |= NV3_PFIFO_CACHE1_PULLER_CONTROL_SOFTWARE_METHOD;
            nv3->pfifo.cache0_settings.puller_control &= ~NV3_PFIFO_CACHE1_PULLER_CONTROL_ENABLED;
        }
            
        // It's an error but it isn't lol   
        nv3_pfifo_interrupt(NV3_PFIFO_INTR_CACHE_ERROR, true);
        
    }
    else
    {
        // obviously turn off the "is software" if it's not
        if (!cache_num)
            nv3->pfifo.cache0_settings.puller_control &= ~NV3_PFIFO_CACHE0_PULLER_CONTROL_SOFTWARE_METHOD;
        else   
            nv3->pfifo.cache1_settings.puller_control &= ~NV3_PFIFO_CACHE1_PULLER_CONTROL_SOFTWARE_METHOD;
    }
    
    // Ok we found it. Lol
    return true; 
    
}

#ifndef RELEASE_BUILD
// Prints out some informaiton about the object
void nv3_debug_ramin_print_context_info(uint32_t name, nv3_ramin_context_t context)
{
    nv_log("Found object:");
    nv_log("Name: 0x%04x", name);

    nv_log("Context:");
    nv_log("DMA Channel %d (0-7 valid)", context.channel);
    nv_log("Class ID: as repreesnted in ramin=%04x, Stupid 5 bit version (the actual id)=0x%04x (%s)", context.class_id, 
    context.class_id & 0x1F, nv3_class_names[context.class_id & 0x1F]);
    nv_log("Render Engine %d (0=Software, also DMA? 1=Accelerated Renderer)", context.is_rendering);
    nv_log("PRAMIN Offset 0x%08x", context.ramin_offset << 4);
}

#endif