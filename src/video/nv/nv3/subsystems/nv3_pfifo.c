/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PFIFO (FIFO for graphics object submission)
 *          PIO object submission
 *          Gray code conversion routines
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>
#include "cpu.h"


static uint32_t nv3_pfifo_cache1_ptr_mask(void);
static uint32_t nv3_pfifo_ramro_ptr_mask(void);
static uint32_t nv3_pfifo_gray_to_binary(uint32_t gray);
uint32_t        nv3_pfifo_cache1_slot(uint32_t ptr);
uint32_t        nv3_pfifo_cache1_next(uint32_t ptr);

/* CACHE1's entries (method at +0, data at +4, 8 bytes each), indexed by the Gray-coded GET/PUT
   index: 32 at 0x3300 on rev A/B; the RIVA 128 ZX's 64 at 0x3400 (NV3RM.VXD's cache-error
   handler reads 0x3400 + GET*2 there). Gives the slot when the address is in the window. */
static bool nv3_pfifo_cache1_method_slot(uint32_t address, uint32_t *slot)
{
    if (nv3->nvbase.gpu_revision >= NV3_PCI_CFG_REVISION_C00) {
        if (address < NV3_PFIFO_CACHE1_METHOD_START_REV_C || address >= NV3_PFIFO_CACHE1_METHOD_END_REV_C)
            return false;

        *slot = ((address - NV3_PFIFO_CACHE1_METHOD_START_REV_C) >> 3) & (NV3_PFIFO_CACHE1_SIZE_REV_C - 1);
    } else {
        if (address < NV3_PFIFO_CACHE1_METHOD_START || address >= NV3_PFIFO_CACHE1_METHOD_END)
            return false;

        *slot = ((address - NV3_PFIFO_CACHE1_METHOD_START) >> 3) & (NV3_PFIFO_CACHE1_SIZE_REV_AB - 1);
    }

    return true;
}

uint32_t nv3_pfifo_read(uint32_t address)
{
    uint32_t slot;

    // before doing anything, check the subsystem enablement state

    if (!((nv3->pmc.enable >> NV3_PMC_ENABLE_PFIFO) & NV3_PMC_ENABLE_PFIFO_ENABLED))
    {
        nv_log("Repressing PFIFO read. The subsystem is disabled according to pmc_enable, returning 0\n");
        return 0x00;
    }

    uint32_t ret = 0x00;

    // todo: friendly logging

    switch (address)
    {
        case NV3_PFIFO_INTR:
            ret = nv3->pfifo.intr;
            break;
        case NV3_PFIFO_INTR_EN:
            ret = nv3->pfifo.intr_en;
            break;
        case NV3_PFIFO_DELAY_0:
            ret = nv3->pfifo.dma_delay_retry;
            break;
        // Debug
        case NV3_PFIFO_DEBUG_0:
            ret = nv3->pfifo.debug_0;

            // update the internal cache error state since the cache_error interrupt was serviced by the Resource Manager.
            // Do thi seven if this interrupt is disabled
            if (nv3->pfifo.intr & NV3_PFIFO_INTR_CACHE_ERROR)
            {
                nv3->pfifo.debug_0 &= ~(1 << NV3_PFIFO_CACHE0_ERROR_PENDING);
                nv3->pfifo.debug_0 &= ~(1 << NV3_PFIFO_CACHE1_ERROR_PENDING);
            }
    
            break;
        case NV3_PFIFO_CONFIG_0:
            ret = nv3->pfifo.config_0;
            break; 
        // Some of these may need to become functions.
        case NV3_PFIFO_CONFIG_RAMFC:
            ret = nv3->pfifo.ramfc_config;
            break;
        case NV3_PFIFO_CONFIG_RAMHT:
            ret = nv3->pfifo.ramht_config;
            break;
        case NV3_PFIFO_CONFIG_RAMRO:
            ret = nv3->pfifo.ramro_config;
            break;
        /* These automatically trigger pulls when 1 is written */
        case NV3_PFIFO_CACHE0_PULL0:
            ret = nv3->pfifo.cache0_settings.pull0;
            break;
        case NV3_PFIFO_CACHE1_PULL0:
            ret = nv3->pfifo.cache1_settings.pull0;
            break;
        case NV3_PFIFO_CACHE0_PULLER_CTX_STATE:
            ret = (nv3->pfifo.cache0_settings.context_is_dirty) ? (1 << NV3_PFIFO_CACHE0_PULLER_CTX_STATE_DIRTY) : 0;
            break;
        case NV3_PFIFO_CACHE1_PULLER_CTX_STATE:
            ret = (nv3->pfifo.cache0_settings.context_is_dirty) ? (1 << NV3_PFIFO_CACHE0_PULLER_CTX_STATE_DIRTY) : 0;
            break;
        /* Does this automatically push? */
        case NV3_PFIFO_CACHE0_PUSH_ENABLED:
            ret = nv3->pfifo.cache0_settings.push0;
            break;
        case NV3_PFIFO_CACHE1_PUSH_ENABLED:
            ret = nv3->pfifo.cache1_settings.push0;
            break; 
        case NV3_PFIFO_CACHE0_PUSH_CHANNEL_ID:
            ret = nv3->pfifo.cache0_settings.channel;
            break;
        case NV3_PFIFO_CACHE1_PUSH_CHANNEL_ID:
            ret = nv3->pfifo.cache1_settings.channel;
            break;
        case NV3_PFIFO_CACHE0_STATUS:  
            // CACHE0 has only one entry so it can only ever be empty or full

            if (nv3->pfifo.cache0_settings.put_address == nv3->pfifo.cache0_settings.get_address)
                ret |= 1 << NV3_PFIFO_CACHE0_STATUS_EMPTY;
            else
                ret |= 1 << NV3_PFIFO_CACHE0_STATUS_FULL;

            break;
        case NV3_PFIFO_CACHE1_STATUS:
            // CACHE1 doesn't...

            if (nv3->pfifo.cache1_settings.put_address == nv3->pfifo.cache1_settings.get_address)
                ret |= 1 << NV3_PFIFO_CACHE1_STATUS_EMPTY;

            // Check if Cache1 (0x7C bytes in size depending on gpu?) is full
            // Based on how the drivers do it
            if (!nv3_pfifo_cache1_num_free_spaces())
                ret |= 1 << NV3_PFIFO_CACHE1_STATUS_FULL;
            
            if (nv3->pfifo.runout_put != nv3->pfifo.runout_get)
                ret |= 1 << NV3_PFIFO_CACHE1_STATUS_RANOUT;

            break;
        case NV3_PFIFO_CACHE0_PUT:
            ret = nv3->pfifo.cache0_settings.put_address;
            break;
        case NV3_PFIFO_CACHE0_GET:
            ret = nv3->pfifo.cache0_settings.get_address;
            break;
        case NV3_PFIFO_CACHE1_PUT:
            ret = nv3->pfifo.cache1_settings.put_address;
            break; 
        case NV3_PFIFO_CACHE1_GET: 
            ret = nv3->pfifo.cache1_settings.get_address;
            break;
        // Reassignment
        case NV3_PFIFO_CACHE_REASSIGNMENT:
            ret = nv3->pfifo.cache_reassignment & 0x01; //1bit meaningful
            break;
        // Cache1 exclusive stuff
        // Control
        case NV3_PFIFO_CACHE1_DMA_CONFIG_0:
            ret = nv3->pfifo.cache1_settings.dma_state;
            break; 
        case NV3_PFIFO_CACHE1_DMA_CONFIG_1:
            ret = nv3->pfifo.cache1_settings.dma_length;
            break;
        case NV3_PFIFO_CACHE1_DMA_CONFIG_2:
            ret = nv3->pfifo.cache1_settings.dma_address;
            break;
        case NV3_PFIFO_CACHE1_DMA_CONFIG_3:
            ret = nv3->pfifo.cache1_settings.dma_target_node;
            break;
        case NV3_PFIFO_CACHE1_DMA_STATUS:
            ret = nv3->pfifo.cache1_settings.dma_status;
            break;
        case NV3_PFIFO_CACHE1_DMA_TLB_PT_BASE:
            ret = nv3->pfifo.cache1_settings.dma_tlb_pt_base;
            break;
        case NV3_PFIFO_CACHE1_DMA_TLB_PTE:
            ret = nv3->pfifo.cache1_settings.dma_tlb_pte;
            break;
        case NV3_PFIFO_CACHE1_DMA_TLB_TAG:
            ret = nv3->pfifo.cache1_settings.dma_tlb_tag;
            break;
        // Runout
        case NV3_PFIFO_RUNOUT_GET:
            ret = nv3->pfifo.runout_get;
            break;
        case NV3_PFIFO_RUNOUT_PUT:
            ret = nv3->pfifo.runout_put;
            break;
        case NV3_PFIFO_RUNOUT_STATUS:
            if (nv3->pfifo.runout_put == nv3->pfifo.runout_get)
                ret |= 1 << NV3_PFIFO_RUNOUT_STATUS_EMPTY; /* good news */
            else 
                ret |= 1 << NV3_PFIFO_RUNOUT_STATUS_RANOUT; /* bad news */

            // full when advancing PUT by one 8-byte entry would hit GET
            if (((nv3->pfifo.runout_put + 0x08) & nv3_pfifo_ramro_ptr_mask()) == nv3->pfifo.runout_get)
                ret |= 1 << NV3_PFIFO_RUNOUT_STATUS_FULL; /* VERY BAD news */

            break;
        
        /* Cache1 is handled below - cache0 only has one entry */
        case NV3_PFIFO_CACHE0_CTX:
            ret = nv3->pfifo.cache0_settings.context[0];
            break;
    }

    /* Handle some special memory areas */
    if (address >= NV3_PFIFO_CACHE1_CTX_START && address < NV3_PFIFO_CACHE1_CTX_END)
    {
        uint32_t ctx_entry_id = ((address - NV3_PFIFO_CACHE1_CTX_START) / 16) % 8;
        ret = nv3->pfifo.cache1_settings.context[ctx_entry_id];

        nv_log_verbose_only("PFIFO Cache1 CTX Read Entry=%d Value=0x%04x\n", ctx_entry_id, ret);
    }
    /* Direct cache read  stuff */
    else if (address >= NV3_PFIFO_CACHE0_METHOD_START && address < NV3_PFIFO_CACHE0_METHOD_END)
    {
        nv_log_verbose_only("PFIFO Cache0 Read\n");

        // See if we want the object name or the channel/subchannel information.
        if (address & 4)
        {
            nv_log_verbose_only("Data=0x%08x\n", nv3->pfifo.cache0_entry.data);
            ret = nv3->pfifo.cache0_entry.data;
        }
        else
        {
            uint32_t final = nv3->pfifo.cache0_entry.method | (nv3->pfifo.cache0_entry.subchannel << NV3_PFIFO_CACHE1_METHOD_SUBCHANNEL);
            nv_log_verbose_only("Param (subchannel=15:13, method=12:2)=0x%08x\n", final);
            ret = final;
        }
    }
    else if (nv3_pfifo_cache1_method_slot(address, &slot))
    {
        nv_log_verbose_only("PFIFO Cache1 Read slot=%d", slot);

        // See if we want the object name or the channel/subchannel information.
        if (address & 4)
        {
            nv_log_verbose_only("Data=0x%08x\n", nv3->pfifo.cache1_entries[slot].data);
            ret = nv3->pfifo.cache1_entries[slot].data;
        }
        else
        {
            uint32_t final = nv3->pfifo.cache1_entries[slot].method | (nv3->pfifo.cache1_entries[slot].subchannel << NV3_PFIFO_CACHE1_METHOD_SUBCHANNEL);
            nv_log_verbose_only("Param (subchannel=15:13, method=12:2)=0x%08x\n", final);
            ret = final;
        }
            
    }

    return ret; 
}

/* The DMA pusher: while DMA_CTRL allows it and DMA_COUNT bytes are left, fetch dwords at
   DMA_GET through the page table at DMA_PT_INST (one-entry TLB in DMA_TLB_TAG/PTE). A header
   (method 12:2, subchannel 15:13, count 28:18) starts a run of count data words for successive
   methods; each goes into CACHE1 as a write to the current channel's USER area would. The
   pusher waits while CACHE1 is full and resumes when the puller has made room. */
static bool nv3_pfifo_dma_pusher_active;

/* The pusher runs a moment after it is started (or unblocked), as the hardware's fetches are
   asynchronous: the Win9x D3D driver arms its notifier only after kicking the push buffer */
void nv3_pfifo_dma_pusher_timer(void *priv)
{
    (void) priv;
    nv3_pfifo_trigger_dma_if_required();
}

void nv3_pfifo_dma_pusher_kick(void)
{
    nv3_pfifo_cache_t *c1 = &nv3->pfifo.cache1_settings;

    if ((c1->dma_state & 1) && (c1->dma_length >= 4) && !timer_is_enabled(&nv3->dma_pusher_timer))
        timer_on_auto(&nv3->dma_pusher_timer, 10.0);
}

/* Run the puller until CACHE1 is empty or PGRAPH stops taking methods (a trap) */
void nv3_pfifo_cache1_drain(void)
{
    for (int i = 0; i < 64; i++) {
        uint32_t get = nv3->pfifo.cache1_settings.get_address;

        if ((get == nv3->pfifo.cache1_settings.put_address) || !nv3->pgraph.fifo_access
            || !(nv3->pfifo.cache1_settings.pull0 & (1 << NV3_PFIFO_CACHE1_PULL0_ENABLED)))
            break;
        nv3_pfifo_cache1_pull();
        if (nv3->pfifo.cache1_settings.get_address == get)
            break;
    }
}

/* Debug: log the next N push buffer headers ("dev nv3 push N") */
uint32_t nv3_push_trace_left;

void nv3_pfifo_trigger_dma_if_required(void)
{
    nv3_pfifo_cache_t *c1 = &nv3->pfifo.cache1_settings;

    if (nv3_pfifo_dma_pusher_active)
        return;
    nv3_pfifo_dma_pusher_active = true;

    while ((c1->dma_state & 1) && (c1->dma_length >= 4) && c1->push0) {
        /* DMA_PT_INST points at the page table (word 2) of the push buffer's DMA object. DMA_GET
           indexes the page table directly: the resman (NV3RM.VXD) folds the object's adjust
           (word 0, bits 11:0) into the GET it programs, so it is not added again -- with it, a
           buffer whose object has adjust 0x80 was read 0x80 bytes late and parsed mid-command */
        uint32_t linear = c1->dma_address;
        uint32_t page   = linear & 0xFFFFF000;
        uint32_t word;

        /* a command word needs a free CACHE1 slot; a header doesn't */
        if (((c1->dma_status >> 18) & 0x7FF) && !nv3_pfifo_cache1_num_free_spaces())
            break;

        if (c1->dma_tlb_tag != page) {
            c1->dma_tlb_pte = nv3_ramin_read32(c1->dma_tlb_pt_base + ((linear >> 12) << 2), nv3);
            c1->dma_tlb_tag = page;
        }
        if (!(c1->dma_tlb_pte & 1)) {
            /* page not present: DMA_PTE interrupt, the driver fixes the TLB and restarts */
            c1->dma_tlb_tag = 0xFFFFFFFF;
            nv3->pfifo.intr |= (1 << 16);
            nv3_pmc_handle_interrupts(true);
            break;
        }
        dma_bm_read((c1->dma_tlb_pte & 0xFFFFF000) | (linear & 0xFFF), (uint8_t *) &word, 4, 4);

        c1->dma_address += 4;
        c1->dma_length -= 4;

        uint32_t count = (c1->dma_status >> 18) & 0x7FF;
        if (!count) {
            extern uint32_t nv3_push_trace_left;
            if (nv3_push_trace_left) {
                nv3_push_trace_left--;
                always_log("nv3: push header %08x at %08x (chan %d, %x bytes left)%c", word, c1->dma_address - 4,
                           c1->channel, c1->dma_length, 10);
            }
            /* a header: method and subchannel in bits 15:2, count in 28:18 */
            c1->dma_status = word & 0x1FFCFFFC;
            continue;
        }

        uint32_t method = c1->dma_status & 0x1FFC;
        uint32_t subch  = (c1->dma_status >> 13) & 7;
        nv3_pfifo_cache1_push(NV3_USER_START | ((c1->channel & 0x7F) << 16) | (subch << 13) | method, word);
        nv3_pfifo_cache1_pull();

        c1->dma_status = (c1->dma_status & ~(0x7FF << 18) & ~0x1FFC) | ((count - 1) << 18) | ((method + 4) & 0x1FFC);
    }

    nv3_pfifo_dma_pusher_active = false;
}

void nv3_pfifo_write(uint32_t address, uint32_t val)
{
    uint32_t slot;

    // before doing anything, check the subsystem enablement

    if (!((nv3->pmc.enable >> NV3_PMC_ENABLE_PFIFO) & NV3_PMC_ENABLE_PFIFO_ENABLED))
    {
        nv_log("Repressing PFIFO write. The subsystem is disabled according to pmc_enable\n");
        return;
    }

    switch (address)
    {
        // Interrupt state:
        // Bit 0 - Cache Error
        // Bit 4 - RAMRO Triggered
        // Bit 8 - RAMRO Overflow (too many invalid dma objects)
        // Bit 12 - DMA Pusher 
        // Bit 16 - DMA Page Table Entry (pagefault?)
        case NV3_PFIFO_INTR:
            nv3->pfifo.intr &= ~val;
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PFIFO_INTR_EN:
            nv3->pfifo.intr_en = val & 0x00011111;
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PFIFO_DELAY_0:
            nv3->pfifo.dma_delay_retry = val;
            break;
        case NV3_PFIFO_CONFIG_0:
            nv3->pfifo.config_0 = val;
            break;
        case NV3_PFIFO_CONFIG_RAMHT:
            nv3->pfifo.ramht_config = val;
// This code sucks a bit fix it later
//#ifdef ENABLE_NV_LOG
            nv3->pfifo.ramht_size = ((val >> 16) & 0x03);
            nv3->pfifo.ramht_location = ((nv3->pfifo.ramht_config >> NV3_PFIFO_CONFIG_RAMHT_BASE_ADDRESS) & 0x0F) << 12;

            if (nv3->pfifo.ramht_size == 0)
                nv3->pfifo.ramht_size = 0x1000;
            else if (nv3->pfifo.ramht_size == 1)
                nv3->pfifo.ramht_size = 0x2000;
            else if (nv3->pfifo.ramht_size == 2)
                nv3->pfifo.ramht_size = 0x4000;
            else if (nv3->pfifo.ramht_size == 3)
                nv3->pfifo.ramht_size = 0x8000;  

            nv_log("RAMHT Reconfiguration\n"
            "Base Address in RAMIN: %d\n"
            "Size: 0x%08x bytes\n", nv3->pfifo.ramht_location, nv3->pfifo.ramht_size); 
//#endif
            break;
        case NV3_PFIFO_CONFIG_RAMFC:
            nv3->pfifo.ramfc_config = val;

            nv_log("RAMFC Reconfiguration\n"
            "Base Address in RAMIN: %d\n", ((nv3->pfifo.ramfc_config >> NV3_PFIFO_CONFIG_RAMFC_BASE_ADDRESS) & 0x7F) << 9); 
            break;
        case NV3_PFIFO_CONFIG_RAMRO:
            nv3->pfifo.ramro_config = val;
            nv3->pfifo.ramro_location = ((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_BASE_ADDRESS) & 0x7F) << 9; /* bits 15:9 of the RAMIN address */

            uint32_t new_size_ramro = ((val >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01);

            nv3->pfifo.ramro_size = new_size_ramro ? 0x2000 : 0x200;
            
            nv_log("RAMRO Reconfiguration\n"
            "Base Address in RAMIN: %d\n"
            "Size: 0x%08x bytes\n", nv3->pfifo.ramro_location, new_size_ramro); 
            break;
        case NV3_PFIFO_DEBUG_0:
            nv3->pfifo.debug_0 = val;
            break;
        // Reassignment
        case NV3_PFIFO_CACHE_REASSIGNMENT:
            nv3->pfifo.cache_reassignment = val & 0x01; //1bit meaningful
            break;
        // Control - these can trigger pulls
        case NV3_PFIFO_CACHE0_PULL0:
            nv3->pfifo.cache0_settings.pull0 = val; // 8bits meaningful
            
            if (nv3->pfifo.cache0_settings.pull0 & (1 << NV3_PFIFO_CACHE0_PULL0_ENABLED))
                nv3_pfifo_cache0_pull();

            break;
        case NV3_PFIFO_CACHE1_PULL0:
            nv3->pfifo.cache1_settings.pull0 = val; // 8bits meaningful
            
            if (nv3->pfifo.cache1_settings.pull0 & (1 << NV3_PFIFO_CACHE1_PULL0_ENABLED))
                nv3_pfifo_cache1_pull();

            break;
        case NV3_PFIFO_CACHE0_PULLER_CTX_STATE:
            nv3->pfifo.cache0_settings.context_is_dirty = (val >> NV3_PFIFO_CACHE0_PULLER_CTX_STATE_DIRTY) & 0x01;
            break;
        case NV3_PFIFO_CACHE1_PULLER_CTX_STATE:
            nv3->pfifo.cache1_settings.context_is_dirty = (val >> NV3_PFIFO_CACHE0_PULLER_CTX_STATE_DIRTY) & 0x01;
            break;
        case NV3_PFIFO_CACHE0_PUSH_ENABLED:
            nv3->pfifo.cache0_settings.push0 = val;
            break;
        case NV3_PFIFO_CACHE1_PUSH_ENABLED:
            nv3->pfifo.cache1_settings.push0 = val;
            break; 
        case NV3_PFIFO_CACHE0_PUSH_CHANNEL_ID:
            nv3->pfifo.cache0_settings.channel = val;
            break;
        case NV3_PFIFO_CACHE1_PUSH_CHANNEL_ID:
            nv3->pfifo.cache1_settings.channel = val;
            break;
        // CACHE0_STATUS and CACHE1_STATUS are not writable
        // DMA configuration
        case NV3_PFIFO_CACHE1_DMA_CONFIG_0:
            nv3->pfifo.cache1_settings.dma_state = val;
            break; 
        case NV3_PFIFO_CACHE1_DMA_CONFIG_1:
            nv3->pfifo.cache1_settings.dma_length = val;
            break;
        case NV3_PFIFO_CACHE1_DMA_CONFIG_2:
            nv3->pfifo.cache1_settings.dma_address = val;
            break;
        case NV3_PFIFO_CACHE1_DMA_CONFIG_3:
            nv3->pfifo.cache1_settings.dma_target_node = val & 3;
            break;
        case NV3_PFIFO_CACHE1_DMA_STATUS:
            nv3->pfifo.cache1_settings.dma_status = val;
            break;
        case NV3_PFIFO_CACHE1_DMA_TLB_PT_BASE:
            nv3->pfifo.cache1_settings.dma_tlb_pt_base = val;
            break;
        case NV3_PFIFO_CACHE1_DMA_TLB_PTE:
            nv3->pfifo.cache1_settings.dma_tlb_pte = val;
            break;
        case NV3_PFIFO_CACHE1_DMA_TLB_TAG:
            nv3->pfifo.cache1_settings.dma_tlb_tag = val;
            break;
        /* Put and Get addresses */
        case NV3_PFIFO_CACHE0_PUT:
            nv3->pfifo.cache0_settings.put_address = val;
            break;
        case NV3_PFIFO_CACHE0_GET:
            nv3->pfifo.cache0_settings.get_address = val;
            break;
        case NV3_PFIFO_CACHE1_PUT:
            nv3->pfifo.cache1_settings.put_address = val & (nv3_pfifo_cache1_ptr_mask() << 2);
            break;
        case NV3_PFIFO_CACHE1_GET:
            nv3->pfifo.cache1_settings.get_address = val & (nv3_pfifo_cache1_ptr_mask() << 2);
            break;
        case NV3_PFIFO_RUNOUT_GET:
            nv3->pfifo.runout_get = val & nv3_pfifo_ramro_ptr_mask();
            break;
        case NV3_PFIFO_RUNOUT_PUT:
            nv3->pfifo.runout_put = val & nv3_pfifo_ramro_ptr_mask();
            break;
        /* Cache1 Context is handled below */
        case NV3_PFIFO_CACHE0_CTX:
            nv3->pfifo.cache0_settings.context[0] = val;
            break;
    }

    if (address >= NV3_PFIFO_CACHE0_METHOD_START && address < NV3_PFIFO_CACHE0_METHOD_END)
    {
        nv_log_verbose_only("PFIFO Cache0 Write\n");

        // 3104 always written after 3100
        if (address & 0x04)
        {   
            nv_log_verbose_only("Name = 0x%08x\n", val);
            nv3->pfifo.cache0_entry.data = val;
            nv3_pfifo_cache0_pull(); // immediately pull out
        }
        else
        {
            nv3->pfifo.cache0_entry.method = (val & 0x1FFC);
            nv3->pfifo.cache0_entry.subchannel = (val >> NV3_PFIFO_CACHE1_METHOD_SUBCHANNEL) & 0x07;
            nv_log_verbose_only("Subchannel = 0x%08x, method = 0x%04x\n", nv3->pfifo.cache0_entry.subchannel, nv3->pfifo.cache0_entry.method);
        }

    }
    else if (nv3_pfifo_cache1_method_slot(address, &slot))
    {
        /* Indexed like the read side and the pusher: the slot already is the Gray-coded index */
        uint32_t real_entry = slot;

        nv_log_verbose_only("Cache1 Write Slot %d (Gray code)", real_entry);

        // See if we want the object name or the channel/subchannel information.
        if (address & 4)
        {
            nv_log_verbose_only("Name = 0x%08x\n", val);
            nv3->pfifo.cache1_entries[real_entry].data = val;
        }
        else
        {
            nv3->pfifo.cache1_entries[real_entry].method = (val & 0x1FFC);
            nv3->pfifo.cache1_entries[real_entry].subchannel = (val >> NV3_PFIFO_CACHE1_METHOD_SUBCHANNEL) & 0x07;
            nv_log_verbose_only("Subchannel = 0x%08x, method = 0x%04x\n", nv3->pfifo.cache1_entries[real_entry].subchannel, nv3->pfifo.cache1_entries[real_entry].method);
        }
    }
    /* Handle some special memory areas */
    else if (address >= NV3_PFIFO_CACHE1_CTX_START && address < NV3_PFIFO_CACHE1_CTX_END)
    {
        uint32_t ctx_entry_id = ((address - NV3_PFIFO_CACHE1_CTX_START) / 16) % 8;
        nv3->pfifo.cache1_settings.context[ctx_entry_id] = val;
        {
            extern uint32_t nv3_swm_trace_left;
            if (nv3_swm_trace_left) {
                always_log("nv3: cache1 ctx[%d] = %08x (cs:eip %04x:%08x)%c", ctx_entry_id, val, CS, cpu_state.pc, 10);
            }
        }

        nv_log_verbose_only("PFIFO Cache1 CTX Write Entry=%d value=0x%04x\n", ctx_entry_id, val);
    }

    /* the puller or pusher may have been (re)enabled */
    nv3_pfifo_cache1_drain();
    nv3_pfifo_dma_pusher_kick();
}


/* 
https://en.wikipedia.org/wiki/Gray_code
WHY?????? IT'S NOT A TELEGRAPH IT'S A GPU?????

Convert from a normal number to a total insanity number which is only used in PFIFO CACHE1 for ungodly and totally unknowable reasons 
(Possibly it just makes it easier to implement in logic)

I decided to use a lookup table to save everyone's time, also the numbers generated from the function
that existed here before didn't make any sense
*/

#define NV3_GRAY_TABLE_NUM_ENTRIES 64

uint8_t nv3_pfifo_cache1_gray_code_table[NV3_GRAY_TABLE_NUM_ENTRIES] = {
    0b000000, 0b000001, 0b000011, 0b000010, 0b000110, 0b000111, 0b000101, 0b000100, //0x07
    0b001100, 0b001101, 0b001111, 0b001110, 0b001010, 0b001011, 0b001001, 0b001000, //0x0F
    0b011000, 0b011001, 0b011011, 0b011010, 0b011110, 0b011111, 0b011101, 0b011100, //0x17
    0b010100, 0b010101, 0b010111, 0b010110, 0b010010, 0b010011, 0b010001, 0b010000, //0x1F
    0b110000, 0b110001, 0b110011, 0b110010, 0b110110, 0b110111, 0b110101, 0b110100, //0x27
    0b111100, 0b111101, 0b111111, 0b111110, 0b111010, 0b111011, 0b111001, 0b111000, //0x2F
    0b101000, 0b101001, 0b101011, 0b101010, 0b101110, 0b101111, 0b101101, 0b101100, //0x37
    0b100100, 0b100101, 0b100111, 0b100110, 0b100010, 0b100011, 0b100001, 0b100000  //0x3F
};

/* The function is called up to hundreds of thousands of times per second, it's too slow to do anything else */
uint8_t nv3_pfifo_cache1_binary_code_table[NV3_GRAY_TABLE_NUM_ENTRIES] =
{
    0x00, 0x01, 0x03, 0x02, 0x07, 0x06, 0x04, 0x05, // 0x07 (0)
    0x0F, 0x0E, 0x0C, 0x0D, 0x08, 0x09, 0x0B, 0x0A, // 0x0F (1000)
    0x1F, 0x1E, 0x1C, 0x1D, 0x18, 0x19, 0x1B, 0x1A, // 0x17 (10000)
    0x10, 0x11, 0x13, 0x12, 0x17, 0x16, 0x14, 0x15, // 0x1F (11000)
    0x3F, 0x3E, 0x3C, 0x3D, 0x38, 0x39, 0x3B, 0x3A, // 0x27 (100000)
    0x30, 0x31, 0x33, 0x32, 0x37, 0x36, 0x34, 0x35, // 0x2F (101000)
    0x20, 0x21, 0x23, 0x22, 0x27, 0x26, 0x24, 0x25, // 0x37 (110000)
    0x2F, 0x2E, 0x2C, 0x2D, 0x28, 0x29, 0x2B, 0x2A, // 0X3f (111000)
};

uint32_t nv3_pfifo_cache1_normal2gray(uint32_t val)
{
    return nv3_pfifo_cache1_gray_code_table[val];
}

/* 
Back to sanity
*/
uint32_t nv3_pfifo_cache1_gray2normal(uint32_t val)
{
    return nv3_pfifo_cache1_binary_code_table[val];
}

/* CACHE1's PUT and GET are Gray-coded entry pointers (register bits 2 up). Rev A/B: 5 bits over
   32 entries. The RIVA 128 ZX: 7 bits over 64 entries, one bit more than the ring needs so a full
   ring differs from an empty one -- NV3RM.VXD steps them as gray((binary + 1) & 0x7F) and finds
   the entry at 0x3400 + 8 * the 6-bit Gray code of the binary pointer. */
static uint32_t nv3_pfifo_cache1_ptr_mask(void)
{
    return (nv3->nvbase.gpu_revision >= NV3_PCI_CFG_REVISION_C00) ? 0x7F : 0x1F;
}

/* RUNOUT_GET/PUT are byte offsets of 8-byte RAMRO entries: bits 8:3 (512 bytes) or 12:3 (8 KB).
   The Win95 resman (V128RM.VXD) writes GET = PUT after running a RAMRO entry; masking bit 3
   off there left RAMRO looking non-empty, and it re-ran the same entry forever. */
static uint32_t nv3_pfifo_ramro_ptr_mask(void)
{
    return (((nv3->pfifo.ramro_config >> NV3_PFIFO_CONFIG_RAMRO_SIZE) & 0x01) ? 0x2000 : 0x200) - 0x08;
}

static uint32_t nv3_pfifo_gray_to_binary(uint32_t gray)
{
    gray ^= gray >> 4;
    gray ^= gray >> 2;
    gray ^= gray >> 1;
    return gray;
}

/* The entry a PUT/GET register value points at */
uint32_t nv3_pfifo_cache1_slot(uint32_t ptr)
{
    uint32_t binary = nv3_pfifo_gray_to_binary((ptr >> 2) & nv3_pfifo_cache1_ptr_mask());

    binary &= (nv3->nvbase.gpu_revision >= NV3_PCI_CFG_REVISION_C00) ? (NV3_PFIFO_CACHE1_SIZE_REV_C - 1)
                                                                     : (NV3_PFIFO_CACHE1_SIZE_REV_AB - 1);
    return binary ^ (binary >> 1);
}

/* The PUT/GET register value after this one */
uint32_t nv3_pfifo_cache1_next(uint32_t ptr)
{
    uint32_t mask   = nv3_pfifo_cache1_ptr_mask();
    uint32_t binary = (nv3_pfifo_gray_to_binary((ptr >> 2) & mask) + 1) & mask;

    return (binary ^ (binary >> 1)) << 2;
}

/* 
You can't push into cache0 on the real hardware, but it's not practically done because Cache0 is meant to be reserved for software objects,
NV_USER writes always go to CACHE1
*/

// Pulls graphics objects OUT of cache0
void nv3_pfifo_cache0_pull(void)
{

    // Do nothing if PFIFO CACHE0 is disabled
    if (!(nv3->pfifo.cache0_settings.pull0 & (1 << NV3_PFIFO_CACHE0_PULL0_ENABLED)))
        return; 

    // Do nothing if there is nothing in cache0 to pull
    if (nv3->pfifo.cache0_settings.put_address == nv3->pfifo.cache0_settings.get_address)
        return;

    // PGRAPH is not taking methods (a trapped method is being handled): leave it queued
    if (!nv3->pgraph.fifo_access)
        return;

    // There is only one entry for cache0 
    uint8_t current_channel = nv3->pfifo.cache0_settings.channel;
    uint8_t current_subchannel = nv3->pfifo.cache0_entry.subchannel;
    uint32_t current_param = nv3->pfifo.cache0_entry.data;
    uint16_t current_method = nv3->pfifo.cache0_entry.method;

    // i.e. there is no method in cache0, so we have to find the object.
    if (!current_method)
    {
        // flip the get address over
        nv3->pfifo.cache0_settings.get_address ^= 0x04;

        if (!nv3_ramin_find_object(current_param, 0, current_channel, current_subchannel))
            return; // interrupt was fired, and we went to ramro
    }

    uint32_t current_context = nv3->pfifo.cache0_settings.context[0]; // only 1 entry for CACHE0 so basically ignore the other context entries?
    uint8_t class_id = ((nv3_ramin_context_t*)&current_context)->class_id;

    // Tell the CPU if we found a software method and turn off cache pulling
    if (!(current_context & 0x800000))
    {
        nv_log_verbose_only("The object in CACHE0 is a software object\n");

        nv3->pfifo.cache0_settings.pull0 |= (1 << NV3_PFIFO_CACHE0_PULL0_SOFTWARE_METHOD);
        nv3->pfifo.cache0_settings.pull0 &= ~(1 << NV3_PFIFO_CACHE0_PULL0_ENABLED);
        nv3_pfifo_interrupt(NV3_PFIFO_INTR_CACHE_ERROR, true);
        return;
    }

    /* PGRAPH must hold this channel's context (see the CACHE1 puller) */
    if (!((nv3->pgraph.context_control >> NV3_PGRAPH_CONTEXT_CONTROL_CHID_VALID) & 1)
        || (((nv3->pgraph.context_user >> NV3_PGRAPH_CONTEXT_USER_CHANNEL) & 0x7F) != current_channel)) {
        nv3->pgraph.trapped_address = (current_method & 0x1FFC) | ((uint32_t) (current_subchannel & 7) << 13)
            | ((uint32_t) (class_id & 0x1F) << 16) | ((uint32_t) (current_channel & 0x7F) << 24);
        nv3->pgraph.trapped_data = current_param;
        nv3_pgraph_interrupt_context_switch();
        return;
    }

    // Is this needed?
    nv3->pfifo.cache0_settings.get_address ^= 0x04;

    #ifndef RELEASE_BUILD
    nv_log_verbose_only("***** DEBUG: CACHE0 PULLED ****** Contextual information below\n");
    nv3_ramin_context_t context_structure = *(nv3_ramin_context_t*)&current_context;
    nv3_debug_ramin_print_context_info(current_param, context_structure);
    #endif

    nv3_pgraph_submit(current_param, current_method, current_channel, current_subchannel, class_id & 0x1F, context_structure);

}

/* Debug: log the next N software methods and the register accesses after each ("dev nv3 swm N") */
uint32_t nv3_swm_trace_left;

void nv3_pfifo_context_switch(uint32_t new_channel)
{
    /* The pusher hands CACHE1 to another channel: the puller's eight subchannel contexts are saved
       to the old channel's RAMFC slot and loaded from the new one's (RAMFC: 0x20 bytes per channel,
       0x1000 bytes for 128 channels, base in bits 9-15 of the RAMFC register) (envytools nv1-pfifo) */
    uint32_t ramfc = ((nv3->pfifo.ramfc_config >> NV3_PFIFO_CONFIG_RAMFC_BASE_ADDRESS) & 0x7F) << 9;
    uint32_t old   = nv3->pfifo.cache1_settings.channel & 0x7F;

    new_channel &= 0x7F;
    {
        extern uint32_t nv3_swm_trace_left;
        if (nv3_swm_trace_left) {
            always_log("nv3: pfifo channel %d -> %d (ramfc %04x cfg %08x) ctx7 %08x -> %08x%c", old, new_channel, ramfc,
                       nv3->pfifo.ramfc_config, nv3->pfifo.cache1_settings.context[7],
                       nv3_ramin_read32(ramfc + (new_channel << 5) + 28, nv3), 10);
        }
    }
    for (int sc = 0; sc < 8; sc++)
        nv3_ramin_write32(ramfc + (old << 5) + (sc << 2), nv3->pfifo.cache1_settings.context[sc], nv3);
    for (int sc = 0; sc < 8; sc++)
        nv3->pfifo.cache1_settings.context[sc] = nv3_ramin_read32(ramfc + (new_channel << 5) + (sc << 2), nv3);
    nv3->pfifo.cache1_settings.channel = new_channel;
}

// NV_USER writes go here!
// Pushes graphics objects into cache1
void nv3_pfifo_cache1_push(uint32_t addr, uint32_t param)
{
    bool oh_shit = false;   // RAMRO needed
    nv3_ramin_ramro_reason oh_shit_reason = 0x00; // It's all good for now

    // bit 23 of a ramin dword means it's a write...
    uint32_t new_address = 0;

    uint32_t method_offset = (addr & 0x1FFC); // size of dma object is 0x2000 and some universal methods are implemented at this point, like free
    
    // Up to 128 per envytools?
    uint32_t channel = (addr >> NV3_OBJECT_SUBMIT_CHANNEL) & 0x7F;
    uint32_t subchannel = (addr >> NV3_OBJECT_SUBMIT_SUBCHANNEL) & (NV3_DMA_CHANNELS - 1);

    // first make sure there is even any cache available
    if (!nv3->pfifo.cache1_settings.push0)
    {
        oh_shit = true; 
        oh_shit_reason = nv3_runout_reason_no_cache_available;
        new_address |= (nv3_runout_reason_no_cache_available << NV3_PFIFO_RUNOUT_RAMIN_ERR);
    }
    
    // Check if runout is full
    if (nv3->pfifo.runout_get != nv3->pfifo.runout_put)
    {
        oh_shit = true;
        oh_shit_reason = nv3_runout_reason_cache_ran_out; // ? really ? I guess this means we already ran out..
        new_address |= (nv3_runout_reason_cache_ran_out << NV3_PFIFO_RUNOUT_RAMIN_ERR);
    }

    // no space left
    if (!nv3_pfifo_cache1_num_free_spaces())
    {
        oh_shit = true;
        oh_shit_reason = nv3_runout_reason_free_count_overrun;
        new_address |= (nv3_runout_reason_free_count_overrun << NV3_PFIFO_RUNOUT_RAMIN_ERR);
    }

    // 0x0 is used for creating the object. The rest are reserved nvidia methods
    if (method_offset > 0 && method_offset < 0x100)
    {
        oh_shit = true; 
        oh_shit_reason = nv3_runout_reason_reserved_access;
        new_address |= (nv3_runout_reason_reserved_access << NV3_PFIFO_RUNOUT_RAMIN_ERR);
    }

    // Now check for context switching

    if (channel != nv3->pfifo.cache1_settings.channel)
    {
        // Cache reassignment required
        if (!nv3->pfifo.cache_reassignment 
        || (nv3->pfifo.cache1_settings.get_address != nv3->pfifo.cache1_settings.put_address))
        {
            oh_shit = true;
            oh_shit_reason = nv3_runout_reason_no_cache_available;
            new_address |= (nv3_runout_reason_no_cache_available << NV3_PFIFO_RUNOUT_RAMIN_ERR);
        }
        else if (nv3->pfifo.cache1_settings.push0)
            /* CACHE1 follows the channel even when this write itself runs out (a reserved
               offset): the Win9x D3D driver writes one to move CACHE1 to its channel before
               having the DMA pusher fill it */
            nv3_pfifo_context_switch(channel);
    }

    // Did we fuck up?
    if (oh_shit)
    {
        {
            static int runout_logs;
            if (runout_logs < 30) {
                runout_logs++;
                always_log("nv3: runout reason %d chan %d subch %d mthd %04x data %08x (push0 %d free %d get %x put %x)%c", oh_shit_reason,
                           channel, subchannel, method_offset, param, nv3->pfifo.cache1_settings.push0,
                           nv3_pfifo_cache1_num_free_spaces(), nv3->pfifo.cache1_settings.get_address,
                           nv3->pfifo.cache1_settings.put_address, 10);
            }
        }
         
        /* the entry: the access's offset in USER (method 12:2, subchannel 15:13, channel 22:16),
           a write (bit 23 clear), all bytes enabled (27:24, inverted: 0) and the reason (31:28) */
        new_address |= addr & 0x7FFFFC;
        nv3_ramin_write32(nv3->pfifo.ramro_location + nv3->pfifo.runout_put, new_address, nv3);
        nv3_ramin_write32(nv3->pfifo.ramro_location + nv3->pfifo.runout_put + 4, param, nv3);

        nv3->pfifo.runout_put = (nv3->pfifo.runout_put + 0x08) & nv3_pfifo_ramro_ptr_mask();

        //Fire the interrupt. Also the very bad interrupt...
        if (nv3->pfifo.runout_get == nv3->pfifo.runout_put)
            nv3_pfifo_interrupt(NV3_PFIFO_INTR_RUNOUT_OVERFLOW, true);
        else    
            nv3_pfifo_interrupt(NV3_PFIFO_INTR_RUNOUT, true);

        return;
    }

    // We didn't. Let's put it in CACHE1
    uint32_t current_put_index = nv3_pfifo_cache1_slot(nv3->pfifo.cache1_settings.put_address);
    nv3->pfifo.cache1_entries[current_put_index].subchannel = subchannel;
    nv3->pfifo.cache1_entries[current_put_index].method = method_offset;
    nv3->pfifo.cache1_entries[current_put_index].data = param;

    nv3->pfifo.cache1_settings.put_address = nv3_pfifo_cache1_next(nv3->pfifo.cache1_settings.put_address);

    nv_log_verbose_only("Submitted object [PIO]: Channel %d.%d, Parameter 0x%08x, Method ID 0x%04x (Put Address is now %d)\n",
         channel, subchannel, param, method_offset, nv3->pfifo.cache1_settings.put_address);
   
    // Now we're done. Phew!
}

// Pulls graphics objects OUT of cache1
void nv3_pfifo_cache1_pull(void)
{
    // Do nothing if PFIFO CACHE1 is disabled
    if (!(nv3->pfifo.cache1_settings.pull0 & (1 << NV3_PFIFO_CACHE1_PULL0_ENABLED)))
        return; 

    // Do nothing if there is nothing in cache1 to pull
    if (nv3->pfifo.cache1_settings.put_address == nv3->pfifo.cache1_settings.get_address)
        return;

    // PGRAPH is not taking methods (a trapped method is being handled): leave it queued
    if (!nv3->pgraph.fifo_access)
        return;

    uint32_t get_index = nv3_pfifo_cache1_slot(nv3->pfifo.cache1_settings.get_address);

    uint8_t current_channel = nv3->pfifo.cache1_settings.channel;
    uint8_t current_subchannel = nv3->pfifo.cache1_entries[get_index].subchannel;
    uint32_t current_param = nv3->pfifo.cache1_entries[get_index].data;
    uint16_t current_method = nv3->pfifo.cache1_entries[get_index].method;
  
    // NV_ROOT
    if (!current_method)
    {
        if (!nv3_ramin_find_object(current_param, 1, current_channel, current_subchannel))
            return; // interrupt was fired, and we went to ramro
    }

    // should this be obtained from the grobj? Test on real nv3 h/w after drawrect.nvp works
    uint32_t current_context = nv3->pfifo.cache1_settings.context[current_subchannel]; // get the current subchannel
    uint8_t class_id = ((nv3_ramin_context_t*)&current_context)->class_id;

    // start by incrementing
    uint32_t next_get_address = nv3_pfifo_cache1_next(nv3->pfifo.cache1_settings.get_address);

        // Tell the CPU if we found a software method
    //bit23 unset=software
    //bit23 set=hardware
    if (!(current_context & 0x800000))
    {
        nv_log_verbose_only("The object in CACHE1 is a software object\n");
        {
            extern uint32_t nv3_mmio_trace_left;
            if (nv3_swm_trace_left) {
                nv3_swm_trace_left--;
                always_log("nv3: software method chan %d subch %d mthd %04x data %08x ctx %08x%c", current_channel,
                           current_subchannel, current_method, current_param, current_context, 10);
                nv3_mmio_trace_left = 40;
            }
        }

        nv3->pfifo.cache1_settings.pull0 |= (1 << NV3_PFIFO_CACHE0_PULL0_SOFTWARE_METHOD);
        nv3->pfifo.cache1_settings.pull0 &= ~(1 << NV3_PFIFO_CACHE0_PULL0_ENABLED);
        nv3_pfifo_interrupt(NV3_PFIFO_INTR_CACHE_ERROR, true);
        return;
    }

    /* PGRAPH holds one channel's state: a method for another channel (or with no channel loaded)
       raises its CONTEXT_SWITCH interrupt and waits in the cache while the driver's handler
       saves that state and loads this channel's (NV3RM.VXD writes CTX_USER, CTX_CACHE,
       CTX_SWITCH, the DMA pointers, then CTX_CONTROL with CHID_VALID) */
    if (!((nv3->pgraph.context_control >> NV3_PGRAPH_CONTEXT_CONTROL_CHID_VALID) & 1)
        || (((nv3->pgraph.context_user >> NV3_PGRAPH_CONTEXT_USER_CHANNEL) & 0x7F) != current_channel)) {
        nv3->pgraph.trapped_address = (current_method & 0x1FFC) | ((uint32_t) (current_subchannel & 7) << 13)
            | ((uint32_t) (class_id & 0x1F) << 16) | ((uint32_t) (current_channel & 0x7F) << 24);
        nv3->pgraph.trapped_data = current_param;
        nv3_pgraph_interrupt_context_switch();
        return;
    }

    // Is this needed?
    nv3->pfifo.cache1_settings.get_address = next_get_address;

    #ifndef RELEASE_BUILD
    nv_log_verbose_only("***** DEBUG: CACHE1 PULLED ****** Contextual information below\n");
    nv3_ramin_context_t context_structure = *(nv3_ramin_context_t*)&current_context;
    nv3_debug_ramin_print_context_info(current_param, context_structure);
    #endif
    
    nv3_pgraph_submit(current_param, current_method, current_channel, current_subchannel, class_id & 0x1F, context_structure);
}

// THIS IS PER SUBCHANNEL!
uint32_t nv3_pfifo_cache1_num_free_spaces(void)
{
    // get the index

    uint32_t mask   = nv3_pfifo_cache1_ptr_mask();
    uint32_t get    = nv3_pfifo_gray_to_binary((nv3->pfifo.cache1_settings.get_address >> 2) & mask);
    uint32_t put    = nv3_pfifo_gray_to_binary((nv3->pfifo.cache1_settings.put_address >> 2) & mask);
    uint32_t queued = (put - get) & mask;

    /* Free bytes: rev A/B's 5-bit pointers leave one of the 32 entries empty (31 free at most);
       the ZX's 7-bit pointers use all 64. Reported no higher than 0x7C (GUARANTEED_FIFO_DEPTH,
       what USER +0x10 gives on rev A/B). */
    if (nv3->nvbase.gpu_revision >= NV3_PCI_CFG_REVISION_C00) {
        uint32_t free_bytes = (queued >= NV3_PFIFO_CACHE1_SIZE_REV_C) ? 0 : (NV3_PFIFO_CACHE1_SIZE_REV_C - queued) << 2;

        return (free_bytes > 0x7C) ? 0x7C : free_bytes;
    }

    return ((NV3_PFIFO_CACHE1_SIZE_REV_AB - 1 - queued) << 2) & 0x7C;
}