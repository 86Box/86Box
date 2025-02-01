/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Defines core class names for debugging purposes
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 Connor Hyde
 */
#include <stdio.h>
#include <stdint.h>
#include <86Box/86box.h>
#include <86Box/device.h>
#include <86Box/mem.h>
#include <86box/pci.h>
#include <86Box/rom.h> // DEPENDENT!!!
#include <86Box/video.h>
#include <86box/nv/vid_nv.h>
#include <86Box/nv/vid_nv3.h>

/* These are the object classes AS RECOGNISED BY THE GRAPHICS HARDWARE. */
/* The drivers implement a COMPLETELY DIFFERENT SET OF CLASSES. */

/* THERE CAN ONLY BE 32 CLASSES IN NV3 BECAUSE THE CLASS ID PART OF THE CONTEXT OF A GRAPHICS OBJECT IN PFIFO RAM HASH TABLE IS ONLY 5 BITS LONG! */

const char* nv3_class_names[] = 
{
    "NV3 INVALID class 0x00",
    "NV3 class 0x01: Beta factor",
    "NV3 class 0x02: Render operation",
    "NV3 class 0x03: Chroma key",
    "NV3 class 0x04: Plane mask",
    "NV3 class 0x05: Clipping rectangle",
    "NV3 class 0x06: Pattern",
    "NV3 class 0x07: Rectangle",
    "NV3 class 0x08: Point",
    "NV3 class 0x09: Line",
    "NV3 class 0x0A: Lin (line without starting or ending pixel)",
    "NV3 class 0x0B: Triangle",
    "NV3 class 0x0C: Windows 95 GDI text acceleration",
    "NV3 class 0x0D: Memory to memory format",
    "NV3 class 0x0E: Scaled image from memory",
    "NV3 INVALID class 0x0F",
    "NV3 class 0x10: Blit",
    "NV3 class 0x11: Image",
    "NV3 class 0x12: Bitmap",
    "NV3 INVALID class 0x13",
    "NV3 class 0x14: Transfer to Memory",
    "NV3 class 0x15: Stretched image from CPU",
    "NV3 INVALID class 0x16",
    "NV3 class 0x17: Direct3D 5.0 accelerated textured triangle w/zeta buffer",
    "NV3 class 0x18: Point with zeta buffer",
    "NV3 INVALID class 0x19",
    "NV3 INVALID class 0x1A",
    "NV3 INVALID class 0x1B",
    "NV3 class 0x1C: Image in Memory",
    "NV3 INVALID class 0x1D",
    "NV3 INVALID class 0x1E",
    "NV3 INVALID class 0x1F",
};
