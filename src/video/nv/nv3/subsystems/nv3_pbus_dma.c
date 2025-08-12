/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PBUS DMA: DMA & Notifier Engine
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
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

/* Nvidia DMA Engine */

void nv3_perform_dma_m2mf(nv3_grobj_t grobj)
{    
    // notify object base=grobj_1 >> 12
    uint32_t notify_obj_base = grobj.grobj_1 >> 12; 

    uint32_t notify_obj_info  = nv3_ramin_read32(notify_obj_base, nv3);
    uint32_t notify_obj_limit = nv3_ramin_read32(notify_obj_base + 0x04, nv3);
    uint32_t notify_obj_page  = nv3_ramin_read32(notify_obj_base + 0x08, nv3);

    /* extract some important information*/
    uint32_t info_adjust = notify_obj_info & 0xFFF;
    bool info_pt_present = (notify_obj_info >> NV3_NOTIFICATION_PT_PRESENT) & 0x01;
    uint8_t info_dma_target = (notify_obj_info >> NV3_NOTIFICATION_TARGET) & 0x03;

    /* paging information */
    bool page_is_present = notify_obj_page & 0x01;
    bool page_is_readwrite = (notify_obj_page >> NV3_NOTIFICATION_PAGE_ACCESS);
    uint32_t frame_base = notify_obj_page & 0xFFFFF000;

    // This code is temporary and will probably be moved somewhere else
    // Print torns of debug info
    #ifdef DEBUG
    nv_log_verbose_only("******* WARNING: IF THIS OPERATION FUCKS UP, RANDOM MEMORY WILL BE CORRUPTED, YOUR ENTIRE SYSTEM MAY BE HOSED *******\n");

    nv_log_verbose_only("M2MF DMA Information:\n");
    nv_log_verbose_only("Adjust Value: 0x%08x\n", info_adjust);
    (info_pt_present) ? nv_log_verbose_only("Pagetable Present: True\n") : nv_log_verbose_only("Pagetable Present: False\n");

    switch (info_dma_target)
    {
        case NV3_DMA_TARGET_NODE_VRAM: 
            nv_log_verbose_only("Notification Target: VRAM\n");
            break;
        case NV3_DMA_TARGET_NODE_CART: 
            nv_log_verbose_only("VERY BAD WARNING: Notification detected with Notification Target: Cartridge. THIS SHOULD NEVER HAPPEN!!!!!\n");
            break;
        case NV3_DMA_TARGET_NODE_PCI: 
            (nv3->nvbase.bus_generation == nv_bus_pci) ? nv_log_verbose_only("Notification Target: PCI Bus\n") : nv_log_verbose_only("Notification Target: PCI Bus (On AGP card?)\n");
            break;
        case NV3_DMA_TARGET_NODE_AGP: 
            (nv3->nvbase.bus_generation == nv_bus_agp_1x
                || nv3->nvbase.bus_generation == nv_bus_agp_2x) ? nv_log_verbose_only("Notification Target: AGP Bus\n") : nv_log_verbose_only("Notification Target: AGP Bus (On PCI card?)\n");
            break;
    }

    nv_log_verbose_only("Limit: 0x%08x\n", notify_obj_limit);
    (page_is_present) ? nv_log_verbose_only("Page is present\n") : nv_log_verbose_only("Page is not present\n"); 
    (page_is_readwrite) ? nv_log_verbose_only("Page is read-write\n") : nv_log_verbose_only("Page is read-only\n");
    nv_log_verbose_only("Pageframe Address: 0x%08x\n", frame_base);
    #endif

    // set up the dma transfer. we need to translate to a physical address.
    
    uint32_t final_address = 0;

    /* M2MF DMA only uses HW type */
    
    final_address = frame_base + info_adjust;

    /* send the notification off */
    nv_log("About to send M2MF DMA to 0x%08x (Check target)\n", final_address);

    uint32_t offset_in = (nv3->pgraph.m2mf.offset_in);
    uint32_t offset_out = (nv3->pgraph.m2mf.offset_out);

    uint32_t pitch_in = nv3->pgraph.m2mf.pitch_in;
    uint32_t pitch_out = nv3->pgraph.m2mf.pitch_out; 

    // pitch out surely can't be 0
    if (pitch_out == 0)
        pitch_out = pitch_in;

    uint32_t bytes_per_scanline = nv3->pgraph.m2mf.scanline_length;

    uint8_t increment_in = (nv3->pgraph.m2mf.format) & 0x07;
    uint8_t increment_out = (nv3->pgraph.m2mf.format >> NV3_M2MF_FORMAT_INPUT) & 0x07;
 
    for (uint32_t scanline = 0; scanline < nv3->pgraph.m2mf.num_scanlines; scanline++)
    {
        for (uint32_t pixel = offset_in; pixel < (offset_in + bytes_per_scanline); pixel += increment_in)
        {
            nv3->nvbase.svga.vram[offset_out] = nv3->nvbase.svga.vram[offset_in];
            offset_out += increment_out;
        }

        offset_in += pitch_in;
        offset_out += pitch_out;
    }

    /*
    switch (info_dma_target)
    {
        // for M2MF only NVM target node is used.

        case NV3_DMA_TARGET_NODE_VRAM:
           

            uint32_t* vram_32 = (uint32_t*)nv3->nvbase.svga.vram;

            break;
        case NV3_DMA_TARGET_NODE_PCI:
        case NV3_DMA_TARGET_NODE_AGP:
            // Idk how to implement increments of more than 1 but only 1 increments seem to be used with these.
            uint32_t size_in = nv3->pgraph.m2mf.num_scanlines * nv3->pgraph.m2mf.pitch_in;
            uint32_t size_out = nv3->pgraph.m2mf.num_scanlines * nv3->pgraph.m2mf.pitch_out;

            uint8_t* page_in = calloc(1, size_in);

            for (uint32_t scanline = 0; scanline < nv3->pgraph.m2mf.num_scanlines; scanline++)
            {
                
            }

            dma_bm_read(offset_in, page_in, size_in, size_in);
            dma_bm_write(offset_out, page_in, size_out, size_out);

            break;
    }
*/
    // we're done
    nv3->pgraph.notify_pending = false;
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
    
    // these are only nonzero when there is an error
    notify.info32 = notify.info16 = 0; 

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
    nv_log_verbose_only("******* WARNING: IF THIS OPERATION FUCKS UP, RANDOM MEMORY WILL BE CORRUPTED, YOUR ENTIRE SYSTEM MAY BE HOSED *******\n");

    nv_log_verbose_only("Notification Information:\n");
    nv_log_verbose_only("Adjust Value: 0x%08x\n", info_adjust);
    (info_pt_present) ? nv_log_verbose_only("Pagetable Present: True\n") : nv_log_verbose_only("Pagetable Present: False\n");

    switch (info_notification_target)
    {
        case NV3_DMA_TARGET_NODE_VRAM: 
            nv_log_verbose_only("Notification Target: VRAM\n");
            break;
        case NV3_DMA_TARGET_NODE_CART: 
            nv_log_verbose_only("VERY BAD WARNING: Notification detected with Notification Target: Cartridge. THIS SHOULD NEVER HAPPEN!!!!!\n");
            break;
        case NV3_DMA_TARGET_NODE_PCI: 
            (nv3->nvbase.bus_generation == nv_bus_pci) ? nv_log_verbose_only("Notification Target: PCI Bus\n") : nv_log_verbose_only("Notification Target: PCI Bus (On AGP card?)\n");
            break;
        case NV3_DMA_TARGET_NODE_AGP: 
            (nv3->nvbase.bus_generation == nv_bus_agp_1x
                || nv3->nvbase.bus_generation == nv_bus_agp_2x) ? nv_log_verbose_only("Notification Target: AGP Bus\n") : nv_log_verbose_only("Notification Target: AGP Bus (On PCI card?)\n");
            break;
    }

    nv_log_verbose_only("Limit: 0x%08x\n", notify_obj_limit);
    (page_is_present) ? nv_log_verbose_only("Page is present\n") : nv_log_verbose_only("Page is not present\n"); 
    (page_is_readwrite) ? nv_log_verbose_only("Page is read-write\n") : nv_log_verbose_only("Page is read-only\n");
    nv_log_verbose_only("Pageframe Address: 0x%08x\n", frame_base);
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
    nv_log("About to send hardware notification to 0x%08x (Check target)\n", final_address);
    
    switch (info_notification_target)
    {
        case NV3_DMA_TARGET_NODE_VRAM:

            uint32_t* vram_32 = (uint32_t*)nv3->nvbase.svga.vram;

            // increment by 1 because each index increments by 4
            vram_32[final_address] = (notify.nanoseconds & 0xFFFFFFFF);
            vram_32[final_address + 1] = (notify.nanoseconds >> 32);
            vram_32[final_address + 2] = notify.info32;
            vram_32[final_address + 3] = (notify.info16 | notify.status);
            break;
        case NV3_DMA_TARGET_NODE_PCI:
        case NV3_DMA_TARGET_NODE_AGP:
            dma_bm_write(final_address, (uint8_t*)&notify, sizeof(nv3_notification_t), 4);
            break;
    }

    // we're done
    nv3->pgraph.notify_pending = false;
}