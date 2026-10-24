/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x05 (Clipping rectangle)
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2026 Connor Hyde
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
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

void nv3_class_005_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, UNUSED(nv3_grobj_t grobj))
{
    switch (method_id)
    {
        /* The user clip: position (signed) then size; x in 15:0, y in 31:16 */
        case NV3_CLIP_POSITION:
            nv3->pgraph.uclip_min[0] = nv3->pgraph.uclip_max[0] = (int16_t) (param & 0xFFFF);
            nv3->pgraph.uclip_min[1] = nv3->pgraph.uclip_max[1] = (int16_t) (param >> 16);
            nv_log("Method Execution: Clip Position: %d,%d\n", nv3->pgraph.uclip_min[0], nv3->pgraph.uclip_min[1]);
            break;
        case NV3_CLIP_SIZE:
            nv3->pgraph.uclip_max[0] = nv3->pgraph.uclip_min[0] + (int32_t) (param & 0xFFFF);
            nv3->pgraph.uclip_max[1] = nv3->pgraph.uclip_min[1] + (int32_t) (param >> 16);
            nv_log("Method Execution: Clip Size: %d,%d\n", param & 0xFFFF, param >> 16);
            break;
        default:
            nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            return;
    }
}