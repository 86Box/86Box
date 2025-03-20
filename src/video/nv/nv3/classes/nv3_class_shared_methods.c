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
        // set up the current notification request/object]
        // check for double notifiers.
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
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_INVALID_METHOD);
            return;
    }
}


/* Sees if any notification is required after an object method is executed. If so, executes it... */
void nv3_notify_if_needed(uint32_t name, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    if (!nv3->pgraph.notify_pending)
        return; 

    uint32_t current_notification_object = nv3->pgraph.notifier;
    uint32_t notification_type = ((current_notification_object >> NV3_PGRAPH_NOTIFY_REQUEST_TYPE) & 0x07);

    // check for a software method (0 = hardware, 1 = software)
    if (notification_type != 0)
    {  
        nv_log("Software Notification, firing interrupt");
        nv3_pgraph_interrupt_valid(NV3_PGRAPH_INTR_0_SOFTWARE_NOTIFY);
        //return;
    }
        
    // set up the NvNotification structure
    nv3_notification_t notify = {0}; 
    notify.nanoseconds = nv3->ptimer.time;
    notify.status = NV3_NOTIFICATION_STATUS_DONE_OK; // it should be fine to just signal that it's ok
    
    // this is completely speculative and i have no idea 
    notify.info32 = grobj.grobj_0;

    // notify object base=grobj_1 >> 12
    uint32_t notify_obj_base = grobj.grobj_1 >> 12; 

    uint32_t notify_obj_info  = nv3_ramin_read32(notify_obj_base, nv3);
    uint32_t notify_obj_limit = nv3_ramin_read32(notify_obj_base + 0x04, nv3);
    uint32_t notify_obj_page  = nv3_ramin_read32(notify_obj_base + 0x08, nv3);

    /* extract some important information*/
    uint32_t info_adjust = notify_obj_info & 0xFFF;
    bool info_pt_present = (notify_obj_info >> NV3_NOTIFICATION_PT_PRESENT) & 0x01;
    uint8_t info_notification_target = (notify_obj_info >> NV3_NOTIFICATION_TARGET) & 0x03;

    /* paging information */
    bool page_is_present = notify_obj_page & 0x01;
    bool page_is_readwrite = (notify_obj_page >> NV3_NOTIFICATION_PAGE_ACCESS);
    uint32_t frame_base = notify_obj_page & 0xFFFFF000;

    // This code is temporary and will probably be moved somewhere else
    // Print torns of debug info
    #ifdef DEBUG
    nv_log("******* WARNING: IF THIS OPERATION FUCKS UP, RANDOM MEMORY WILL BE CORRUPTED, YOUR ENTIRE SYSTEM MAY BE HOSED *******\n");

    nv_log("Notification Information:\n");
    nv_log("Adjust Value: 0x%08x\n", info_adjust);
    (info_pt_present) ? nv_log("Pagetable Present: True\n") : nv_log("Pagetable Present: False\n");

    switch (info_notification_target)
    {
        case NV3_NOTIFICATION_TARGET_NVM: 
            nv_log("Notification Target: VRAM\n");
            break;
        case NV3_NOTIFICATION_TARGET_CART: 
            nv_log("VERY BAD WARNING: Notification detected with Notification Target: Cartridge. THIS SHOULD NEVER HAPPEN!!!!!\n");
            break;
        case NV3_NOTIFICATION_TARGET_PCI: 
            (nv3->nvbase.bus_generation == nv_bus_pci) ? nv_log("Notification Target: PCI Bus\n") : nv_log("Notification Target: PCI Bus (On AGP card?)\n");
            break;
        case NV3_NOTIFICATION_TARGET_AGP: 
            (nv3->nvbase.bus_generation == nv_bus_agp_1x
                || nv3->nvbase.bus_generation == nv_bus_agp_2x) ? nv_log("Notification Target: AGP Bus\n") : nv_log("Notification Target: AGP Bus (On PCI card?)\n");
            break;
    }

    nv_log("Limit: 0x%08x\n", notify_obj_limit);
    (page_is_present) ? nv_log("Page is present\n") : nv_log("Page is not present\n"); 
    (page_is_readwrite) ? nv_log("Page is read-write\n") : nv_log("Page is read-only\n");
    nv_log("Pageframe Address: 0x%08x\n", frame_base);
    #endif

    // set up the dma transfer. we need to translate to a physical address.
    
    uint32_t final_address = 0;

    /* Simple case: hardware notification, we can just take the pte since it's based on the type */
    if (notification_type == 0)
    {
        final_address = frame_base + info_adjust;
    }
    else
    {
        // for software we have to calculate the pte index
        uint32_t pte_num = ((notification_type << 4) + info_adjust) >> 12;
        
        /* ramin entries are sorted - 1 object for each pte entry...*/
        final_address = nv3_ramin_read32(notify_obj_base + (0x10 * pte_num) + 8, nv3);
        final_address += (info_adjust & 0xFFF); 
    }

    /* send the notification off */
    nv_log("About to send the notification to 0x%08x (Check target)", final_address);
    switch (info_notification_target)
    {
        case NV3_NOTIFICATION_TARGET_NVM:
            svga_writel_linear(final_address, (notify.nanoseconds & 0xFFFFFFFF), nv3);
            svga_writel_linear(final_address + 4, (notify.nanoseconds >> 32), nv3);
            svga_writel_linear(final_address + 8, notify.info32, nv3);
            svga_writel_linear(final_address + 0x0C, (notify.info16 | notify.status), nv3);
            break;
        case NV3_NOTIFICATION_TARGET_PCI:
        case NV3_NOTIFICATION_TARGET_AGP:
            dma_bm_write(final_address, (uint8_t*)&notify, sizeof(nv3_notification_t), 4);
            break;
    }

    // we're done
    nv3->pgraph.notify_pending = false;
}
