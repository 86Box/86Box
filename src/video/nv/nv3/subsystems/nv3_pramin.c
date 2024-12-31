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
    addr &= (nv3->nvbase.svga.vram_max - 1);
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max- 0x10);

    uint8_t val = nv3->nvbase.svga.vram[addr];

    nv_log("NV3: Read byte from RAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);

    return val;
}

// Read 16-bit ramin
uint16_t nv3_ramin_read16(uint32_t addr, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv3->nvbase.svga;
    uint16_t* vram_16bit = (uint16_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max - 0x10);
    addr >>= 1; // what

    uint16_t val = vram_16bit[addr]; // what

    nv_log("NV3: Read word from RAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);

    return val;
}

// Read 32-bit ramin
uint32_t nv3_ramin_read32(uint32_t addr, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv3->nvbase.svga;
    uint32_t* vram_32bit = (uint32_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max - 0x10);
    addr >>= 2; // what

    uint32_t val = vram_32bit[addr];

    nv_log("NV3: Read dword from RAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);

    return val;
}

// Write 8-bit ramin
void nv3_ramin_write8(uint32_t addr, uint8_t val, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_max - 1);
    uint32_t raw_addr = addr; // saved after and

    // Structures in RAMIN are stored from the bottom of vram up in reverse order
    // this can be explained without bitwise math like so:
    // real VRAM address = VRAM_size - (ramin_address - (ramin_address % reversal_unit_size)) - reversal_unit_size + (ramin_address % reversal_unit_size) 
    // reversal unit size in this case is 16 bytes, vram size is 2-8mb (but 8mb is zx/nv3t only and 2mb...i haven't found a 22mb card)
    addr ^= (nv3->nvbase.svga.vram_max - 0x10);

    nv3->nvbase.svga.vram[addr] = val;

    nv_log("NV3: Write byte to RAMIN addr=0x%08x val=0x%02x (raw address=0x%08x)\n", addr, val, raw_addr);
}

// Write 16-bit ramin
void nv3_ramin_write16(uint32_t addr, uint16_t val, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv3->nvbase.svga;
    uint16_t* vram_16bit = (uint16_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max - 0x10);
    addr >>= 1; // what

    vram_16bit[addr] = val;

    nv_log("NV3: Write word to RAMIN addr=0x%08x val=0x%04x (raw address=0x%08x)\n", addr, raw_addr);
}

// Write 32-bit ramin
void nv3_ramin_write32(uint32_t addr, uint32_t val, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv3->nvbase.svga;
    uint32_t* vram_32bit = (uint32_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv3->nvbase.svga.vram_max - 0x10);
    addr >>= 2; // what

    vram_32bit[addr] = val;

    nv_log("NV3: Write dword to RAMIN addr=0x%08x val=0x%08x (raw address=0x%08x)\n", addr, val, raw_addr);
}
