/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods shared across multiple classes
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 Connor Hyde
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

void nv3_generic_method(uint32_t name, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{

}

void nv3_notify()
{
    
}