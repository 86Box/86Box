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
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

void nv3_generic_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id)
    {
        /* mthdCreate in software(?)*/
        case NV3_ROOT_HI_IM_OBJECT_MCOBJECTYFACE:
            //nv_log("mthdCreate obj_name=0x%08x\n", param);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            break;
        // set up the current notification request/object
        // and check for double notifiers.
        case NV3_SET_NOTIFY:
            if (nv3->pgraph.notify_pending)
            {
                nv_log("Executed method NV3_SET_NOTIFY with nv3->pgraph.notify_pending already set. param=0x%08x, method=0x%04x, grobj=0x%08x 0x%08x 0x%08x 0x%08x\n");
                nv_log("IF THIS IS A DEBUG BUILD, YOU SHOULD SEE A CONTEXT BELOW");
                nv3_debug_ramin_print_context_info(param, context);
                nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_DOUBLE_NOTIFY);
                
                // disable
                nv3->pgraph.notify_pending = false;
                nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_DOUBLE_NOTIFY);
                /* may need to disable fifo in this state */
                return; 
            }

            // set a notify as pending.
            nv3->pgraph.notifier = param; 
            nv3->pgraph.notify_pending = true; 
            break;
        default:
            nv_log("Shared Generic Methods: Invalid or Unimplemented method 0x%04x", method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            return;
    }
}
