/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x0D (Reformat image in memory)
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
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

void nv3_class_00d_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id)
    {
        case NV3_M2MF_IN_CTXDMA_OFFSET:
            nv3->pgraph.m2mf.offset_in = param;
            nv_log("Method Execution: M2MF Offset In = 0x%08x", param);
            break;  
        case NV3_M2MF_OUT_CTXDMA_OFFSET:
            nv3->pgraph.m2mf.offset_out = param;
            nv_log("Method Execution: M2MF Offset Out = 0x%08x", param);
            break;  
        case NV3_M2MF_IN_PITCH:
            nv3->pgraph.m2mf.pitch_in = param;
            nv_log("Method Execution: M2MF Pitch In = 0x%08x", param);
            break;  
        case NV3_M2MF_OUT_PITCH:
            nv3->pgraph.m2mf.pitch_out = param;
            nv_log("Method Execution: M2MF Pitch Out = 0x%08x", param);
            break;  
        case NV3_M2MF_SCANLINE_LENGTH_IN_BYTES:
            nv3->pgraph.m2mf.scanline_length = param;
            nv_log("Method Execution: M2MF Scanline Length in Bytes = 0x%08x", param);
            break;  
        case NV3_M2MF_NUM_SCANLINES:
            nv3->pgraph.m2mf.num_scanlines = param;
            nv_log("Method Execution: M2MF Num Scanlines = 0x%08x", param);
            break; 
        case NV3_M2MF_FORMAT:
            nv3->pgraph.m2mf.format = param; 
            nv_log("Method Execution: M2MF Format = 0x%08x", param);
            break;
        case NV3_M2MF_NOTIFY:
            /* BUFFER_NOTIFY starts the transfer, then writes the notifier (the Win9x driver waits on slot 0) */
            /* A DMA fault leaves the notifier to the driver's PDMA interrupt handler */
            if (nv3_perform_dma_m2mf(grobj))
                nv3_write_notifier(grobj, 0, NV3_NOTIFICATION_STATUS_DONE_OK, 0, 0);
            break;                            
        default:
            nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            break;;
    }
}