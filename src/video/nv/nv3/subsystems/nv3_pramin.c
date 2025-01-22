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
#include <86Box/86box.h>
#include <86Box/device.h>
#include <86Box/mem.h>
#include <86box/pci.h>
#include <86Box/rom.h> // DEPENDENT!!!
#include <86Box/video.h>
#include <86Box/nv/vid_nv.h>
#include <86Box/nv/vid_nv3.h>

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

    if (!nv3_pramin_arbitrate_read(addr, &val)) // Oh well
    {
        val = (uint8_t)nv3->nvbase.svga.vram[addr];
        nv_log("NV3: Read byte from PRAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);
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

    if (!nv3_pramin_arbitrate_read(addr, &val))
    {
        val = (uint16_t)vram_16bit[addr];
        nv_log("NV3: Read word from PRAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);
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

    if (!nv3_pramin_arbitrate_read(addr, &val))
    {
        val = vram_32bit[addr];

        nv_log("NV3: Read dword from PRAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);
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

    uint32_t val32 = 0x00;

    if (!nv3_pramin_arbitrate_write(addr, val32))
    {
        nv3->nvbase.svga.vram[addr] = val;
        nv_log("NV3: Write byte to PRAMIN addr=0x%08x val=0x%02x (raw address=0x%08x)\n", addr, val, raw_addr);
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

    uint32_t val32 = 0x00;

    if (!nv3_pramin_arbitrate_write(addr, val32))
    {
        vram_16bit[addr] = val;
        nv_log("NV3: Write word to PRAMIN addr=0x%08x val=0x%04x (raw address=0x%08x)\n", addr, val, raw_addr);
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

    uint32_t val32 = 0x00;

    if (!nv3_pramin_arbitrate_write(addr, val32))
    {
        vram_32bit[addr] = val;
        nv_log("NV3: Write dword to PRAMIN addr=0x%08x val=0x%08x (raw address=0x%08x)\n", addr, val, raw_addr);
    }

}

/* 
RAMIN access arbitration functions
Arbitrates reads and writes to RAMFC (unused dma context storage), RAMRO (invalid object submission location), RAMHT (hashtable for graphics objectstorage) (RAMAU?) 
and generic RAMIN

Takes a pointer to a result integer. This is because we need to check its result in our normal write function.
Returns true if a valid "non-generic" address was found (e.g. RAMFC/RAMRO/RAMHT). False if the specified address is a generic RAMIN address
*/
bool nv3_pramin_arbitrate_read(uint32_t address, uint32_t* value)
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
            ramht_end = ramht_start + NV3_PRAMIN_RAMHT_SIZE_0;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_8K:
            ramht_end = ramht_start + NV3_PRAMIN_RAMHT_SIZE_1;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_16K:
            ramht_end = ramht_start + NV3_PRAMIN_RAMHT_SIZE_2;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_32K:
            ramht_end = ramht_start + NV3_PRAMIN_RAMHT_SIZE_3;
            break;
    }

    switch (ramro_size)
    {
        case NV3_PFIFO_CONFIG_RAMRO_SIZE_512B:
            ramro_end = ramro_start + NV3_PRAMIN_RAMRO_SIZE_0;
            break;
        case NV3_PFIFO_CONFIG_RAMRO_SIZE_8K:
            ramro_end = ramro_start + NV3_PRAMIN_RAMRO_SIZE_1;
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

bool nv3_pramin_arbitrate_write(uint32_t address, uint32_t value) 
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
            ramht_end = ramht_start + NV3_PRAMIN_RAMHT_SIZE_0;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_8K:
            ramht_end = ramht_start + NV3_PRAMIN_RAMHT_SIZE_1;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_16K:
            ramht_end = ramht_start + NV3_PRAMIN_RAMHT_SIZE_2;
            break;
        case NV3_PFIFO_CONFIG_RAMHT_SIZE_32K:
            ramht_end = ramht_start + NV3_PRAMIN_RAMHT_SIZE_3;
            break;
    }

    switch (ramro_size)
    {
        case NV3_PFIFO_CONFIG_RAMRO_SIZE_512B:
            ramro_end = ramro_start + NV3_PRAMIN_RAMRO_SIZE_0;
            break;
        case NV3_PFIFO_CONFIG_RAMRO_SIZE_8K:
            ramro_end = ramro_start + NV3_PRAMIN_RAMRO_SIZE_1;
            break;
    }

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