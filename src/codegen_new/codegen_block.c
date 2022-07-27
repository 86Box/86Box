#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86_ops.h"
#include "x87.h"

#include "386_common.h"

#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_reg.h"

uint8_t *block_write_data = NULL;

int codegen_flat_ds, codegen_flat_ss;
int mmx_ebx_ecx_loaded;
int codegen_flags_changed = 0;
int codegen_fpu_entered = 0;
int codegen_mmx_entered = 0;
int codegen_fpu_loaded_iq[8];
x86seg *op_ea_seg;
int op_ssegs;
uint32_t op_old_pc;

uint32_t recomp_page = -1;

int block_current = 0;
static int block_num;
int block_pos;

uint32_t codegen_endpc;

int codegen_block_cycles;
static int codegen_block_ins;
static int codegen_block_full_ins;

static uint32_t last_op32;
static x86seg *last_ea_seg;
static int last_ssegs;

#ifdef DEBUG_EXTRA
uint32_t instr_counts[256*256];
#endif

static uint16_t block_free_list;
static void delete_block(codeblock_t *block);
static void delete_dirty_block(codeblock_t *block);

/*Temporary list of code blocks that have recently been evicted. This allows for
  some historical state to be kept when a block is the target of self-modifying
  code.

  The size of this list is limited to DIRTY_LIST_MAX_SIZE blocks. When this is
  exceeded the oldest entry will be moved to the free list.*/
static uint16_t block_dirty_list_head, block_dirty_list_tail;
static int dirty_list_size = 0;
#define DIRTY_LIST_MAX_SIZE 64

static void block_free_list_add(codeblock_t *block)
{
#ifndef RELEASE_BUILD
        if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
                fatal("block_free_list_add: block=%p in dirty list\n", block);
#endif
        if (block_free_list)
                block->next = block_free_list;
        else
                block->next = 0;
        block_free_list = get_block_nr(block);
        block->flags = CODEBLOCK_IN_FREE_LIST;
}

static void block_dirty_list_add(codeblock_t *block)
{
#ifndef RELEASE_BUILD
        if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
                fatal("block_dirty_list_add: block=%p already in dirty list\n", block);
#endif
        if (block_dirty_list_head != BLOCK_INVALID)
        {
                codeblock_t *old_head = &codeblock[block_dirty_list_head];

                block->next = block_dirty_list_head;
                block->prev = BLOCK_INVALID;
                block_dirty_list_head = old_head->prev = get_block_nr(block);
        }
        else
        {
                /*List empty*/
                block->prev = block->next = BLOCK_INVALID;
                block_dirty_list_head = block_dirty_list_tail = get_block_nr(block);
        }
        block->flags |= CODEBLOCK_IN_DIRTY_LIST;
        dirty_list_size++;
        if (dirty_list_size > DIRTY_LIST_MAX_SIZE)
        {
                /*Evict oldest block to the free list*/
                codeblock_t *evict_block = &codeblock[block_dirty_list_tail];

#ifndef RELEASE_BUILD
                if (!(evict_block->flags & CODEBLOCK_IN_DIRTY_LIST))
                        fatal("block_dirty_list_add: evict_block=%p %x %x not in dirty list\n", evict_block, evict_block->phys, evict_block->flags);
                if (!block_dirty_list_tail)
                        fatal("block_dirty_list_add - !block_dirty_list_tail\n");
                if (evict_block->prev == BLOCK_INVALID)
                        fatal("block_dirty_list_add - evict_block->prev == BLOCK_INVALID\n");
#endif

                block_dirty_list_tail = evict_block->prev;
                codeblock[evict_block->prev].next = BLOCK_INVALID;

                dirty_list_size--;
                evict_block->flags &= ~CODEBLOCK_IN_DIRTY_LIST;
                delete_dirty_block(evict_block);
        }
}

static void block_dirty_list_remove(codeblock_t *block)
{
        codeblock_t *prev_block = &codeblock[block->prev];
        codeblock_t *next_block = &codeblock[block->next];

#ifndef RELEASE_BUILD
        if (!(block->flags & CODEBLOCK_IN_DIRTY_LIST))
                fatal("block_dirty_list_remove: block=%p not in dirty list\n", block);
#endif

        /*Is block head of list*/
        if (block->prev == BLOCK_INVALID)
                block_dirty_list_head = block->next;
        else
                prev_block->next = block->next;

        /*Is block tail of list?*/
        if (block->next == BLOCK_INVALID)
                block_dirty_list_tail = block->prev;
        else
                next_block->prev = block->prev;

        dirty_list_size--;
#ifndef RELEASE_BUILD
        if (dirty_list_size < 0)
                fatal("remove - dirty_list_size < 0!\n");
#endif
        block->flags &= ~CODEBLOCK_IN_DIRTY_LIST;
}

int codegen_purge_purgable_list()
{
        if (purgable_page_list_head)
        {
                page_t *page = &pages[purgable_page_list_head];

                if (page->code_present_mask & page->dirty_mask)
                {
                        codegen_check_flush(page, page->dirty_mask, purgable_page_list_head << 12);

                        if (block_free_list)
                                return 1;
                }
        }
        return 0;
}

static codeblock_t *block_free_list_get()
{
        codeblock_t *block = NULL;

        while (!block_free_list)
        {
                /*Free list is empty, check the dirty list*/
                if (block_dirty_list_tail)
                {
#ifndef RELEASE_BUILD
                        if (dirty_list_size <= 0)
                                fatal("get - dirty_list_size <= 0!\n");
#endif
                        /*Reuse oldest block*/
                        block = &codeblock[block_dirty_list_tail];

                        block_dirty_list_tail = block->prev;
                        if (block->prev == BLOCK_INVALID)
                                block_dirty_list_head = BLOCK_INVALID;
                        else
                                codeblock[block->prev].next = BLOCK_INVALID;
                        dirty_list_size--;
                        block->flags &= ~CODEBLOCK_IN_DIRTY_LIST;
                        delete_dirty_block(block);
                        block_free_list = get_block_nr(block);
                        break;
                }
                /*Free list is empty - free up a block*/
                if (!codegen_purge_purgable_list())
                        codegen_delete_random_block(0);
        }

        block = &codeblock[block_free_list];
        block_free_list = block->next;
        block->flags &= ~CODEBLOCK_IN_FREE_LIST;
        block->next = 0;
        return block;
}

void codegen_init()
{
        int c;

        codegen_allocator_init();

        codegen_backend_init();
        block_free_list = 0;
        for (c = 0; c < BLOCK_SIZE; c++)
                block_free_list_add(&codeblock[c]);
        block_dirty_list_head = block_dirty_list_tail = 0;
        dirty_list_size = 0;
#ifdef DEBUG_EXTRA
        memset(instr_counts, 0, sizeof(instr_counts));
#endif
}

void codegen_close()
{
#ifdef DEBUG_EXTRA
        pclog("Instruction counts :\n");
        while (1)
        {
                int c;
                uint32_t highest_num = 0, highest_idx = 0;

                for (c = 0; c < 256*256; c++)
                {
                        if (instr_counts[c] > highest_num)
                        {
                                highest_num = instr_counts[c];
                                highest_idx = c;
                        }
                }
                if (!highest_num)
                        break;

                instr_counts[highest_idx] = 0;
                if (highest_idx > 256)
                        pclog(" %02x %02x = %u\n", highest_idx >> 8, highest_idx & 0xff, highest_num);
                else
                        pclog("    %02x = %u\n", highest_idx & 0xff, highest_num);
        }
#endif
}

void codegen_reset()
{
        int c;

        for (c = 1; c < BLOCK_SIZE; c++)
        {
                codeblock_t *block = &codeblock[c];

                if (block->pc != BLOCK_PC_INVALID)
                {
                        block->phys = 0;
                        block->phys_2 = 0;
                        delete_block(block);
                }
        }

        memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
        memset(codeblock_hash, 0, HASH_SIZE * sizeof(uint16_t));
        mem_reset_page_blocks();

        block_free_list = 0;
        for (c = 0; c < BLOCK_SIZE; c++)
        {
                codeblock[c].pc = BLOCK_PC_INVALID;
                block_free_list_add(&codeblock[c]);
        }
}

void dump_block()
{
/*        codeblock_t *block = pages[0x119000 >> 12].block;

        pclog("dump_block:\n");
        while (block)
        {
                uint32_t start_pc = (block->pc & 0xffc) | (block->phys & ~0xfff);
                uint32_t end_pc = (block->endpc & 0xffc) | (block->phys & ~0xfff);
                pclog(" %p : %08x-%08x  %08x-%08x %p %p\n", (void *)block, start_pc, end_pc,  block->pc, block->endpc, (void *)block->prev, (void *)block->next);
                if (!block->pc)
                        fatal("Dead PC=0\n");

                block = block->next;
        }
        pclog("dump_block done\n");*/
}

static void add_to_block_list(codeblock_t *block)
{
        uint16_t block_prev_nr = pages[block->phys >> 12].block;
        uint16_t block_nr = get_block_nr(block);

#ifndef RELEASE_BUILD
        if (!block->page_mask)
                fatal("add_to_block_list - mask = 0 %llx %llx\n", block->page_mask,block->page_mask2);
#endif

        if (block_prev_nr)
        {
                block->next = block_prev_nr;
                codeblock[block_prev_nr].prev = block_nr;
                pages[block->phys >> 12].block = block_nr;
        }
        else
        {
                block->next = BLOCK_INVALID;
                pages[block->phys >> 12].block = block_nr;
        }

        if (block->next)
        {
#ifndef RELEASE_BUILD
                if (codeblock[block->next].pc == BLOCK_PC_INVALID)
                        fatal("block->next->pc=BLOCK_PC_INVALID %p %p %x %x\n", (void *)&codeblock[block->next], (void *)codeblock, block_current, block_pos);
#endif
        }

        if (block->page_mask2)
        {
                block->flags |= CODEBLOCK_HAS_PAGE2;

                block_prev_nr = pages[block->phys_2 >> 12].block_2;

                if (block_prev_nr)
                {
                        block->next_2 = block_prev_nr;
                        codeblock[block_prev_nr].prev_2 = block_nr;
                        pages[block->phys_2 >> 12].block_2 = block_nr;
                }
                else
                {
                        block->next_2 = BLOCK_INVALID;
                        pages[block->phys_2 >> 12].block_2 = block_nr;
                }
        }
}

static void remove_from_block_list(codeblock_t *block, uint32_t pc)
{
        if (!block->page_mask)
                return;
#ifndef RELEASE_BUILD
        if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
                fatal("remove_from_block_list: in dirty list\n");
#endif
        if (block->prev)
        {
                codeblock[block->prev].next = block->next;
                if (block->next)
                        codeblock[block->next].prev = block->prev;
        }
        else
        {
                pages[block->phys >> 12].block = block->next;
                if (block->next)
                        codeblock[block->next].prev = BLOCK_INVALID;
                else
                        mem_flush_write_page(block->phys, 0);
        }

        if (!(block->flags & CODEBLOCK_HAS_PAGE2))
        {
#ifndef RELEASE_BUILD
                if (block->prev_2 || block->next_2)
                        fatal("Invalid block_2 %x %p %08x\n", block->flags, block, block->phys);
#endif
                return;
        }
        block->flags &= ~CODEBLOCK_HAS_PAGE2;

        if (block->prev_2)
        {
                codeblock[block->prev_2].next_2 = block->next_2;
                if (block->next_2)
                        codeblock[block->next_2].prev_2 = block->prev_2;
        }
        else
        {
                pages[block->phys_2 >> 12].block_2 = block->next_2;
                if (block->next_2)
                        codeblock[block->next_2].prev_2 = BLOCK_INVALID;
                else
                        mem_flush_write_page(block->phys_2, 0);
        }
}

static void invalidate_block(codeblock_t *block)
{
        uint32_t old_pc = block->pc;

#ifndef RELEASE_BUILD
        if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
                fatal("invalidate_block: already in dirty list\n");
        if (block->pc == BLOCK_PC_INVALID)
                fatal("Invalidating deleted block\n");
#endif
        remove_from_block_list(block, old_pc);
        block_dirty_list_add(block);
        if (block->head_mem_block)
                codegen_allocator_free(block->head_mem_block);
        block->head_mem_block = NULL;
}

static void delete_block(codeblock_t *block)
{
        uint32_t old_pc = block->pc;

        if (block == &codeblock[codeblock_hash[HASH(block->phys)]])
                codeblock_hash[HASH(block->phys)] = BLOCK_INVALID;

#ifndef RELEASE_BUILD
        if (block->pc == BLOCK_PC_INVALID)
                fatal("Deleting deleted block\n");
#endif
        block->pc = BLOCK_PC_INVALID;

        codeblock_tree_delete(block);
        if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
                block_dirty_list_remove(block);
        else
                remove_from_block_list(block, old_pc);
        if (block->head_mem_block)
                codegen_allocator_free(block->head_mem_block);
        block->head_mem_block = NULL;
        block_free_list_add(block);
}

static void delete_dirty_block(codeblock_t *block)
{
        if (block == &codeblock[codeblock_hash[HASH(block->phys)]])
                codeblock_hash[HASH(block->phys)] = BLOCK_INVALID;

#ifndef RELEASE_BUILD
        if (block->pc == BLOCK_PC_INVALID)
                fatal("Deleting deleted block\n");
#endif
        block->pc = BLOCK_PC_INVALID;

        codeblock_tree_delete(block);
        block_free_list_add(block);
}

void codegen_delete_block(codeblock_t *block)
{
        if (block->pc != BLOCK_PC_INVALID)
                delete_block(block);
}

void codegen_delete_random_block(int required_mem_block)
{
        int block_nr = rand() & BLOCK_MASK;

        while (1)
        {
                if (block_nr && block_nr != block_current)
                {
                        codeblock_t *block = &codeblock[block_nr];

                        if (block->pc != BLOCK_PC_INVALID && (!required_mem_block || block->head_mem_block))
                        {
                                delete_block(block);
                                return;
                        }
                }
                block_nr = (block_nr + 1) & BLOCK_MASK;
        }
}

void codegen_check_flush(page_t *page, uint64_t mask, uint32_t phys_addr)
{
        uint16_t block_nr = page->block;
        int remove_from_evict_list = 0;
        int c;

        while (block_nr)
        {
                codeblock_t *block = &codeblock[block_nr];
                uint16_t next_block = block->next;

                if (*block->dirty_mask & block->page_mask)
                {
                        invalidate_block(block);
                }
#ifndef RELEASE_BUILD
                if (block_nr == next_block)
                        fatal("Broken 1\n");
#endif
                block_nr = next_block;
        }

        block_nr = page->block_2;

        while (block_nr)
        {
                codeblock_t *block = &codeblock[block_nr];
                uint16_t next_block = block->next_2;

                if (*block->dirty_mask2 & block->page_mask2)
                {
                        invalidate_block(block);
                }
#ifndef RELEASE_BUILD
                if (block_nr == next_block)
                        fatal("Broken 2\n");
#endif
                block_nr = next_block;
        }

        if (page->code_present_mask & page->dirty_mask)
                remove_from_evict_list = 1;
        page->code_present_mask &= ~page->dirty_mask;
        page->dirty_mask = 0;

        for (c = 0; c < 64; c++)
        {
                if (page->byte_code_present_mask[c] & page->byte_dirty_mask[c])
                        remove_from_evict_list = 0;
                page->byte_code_present_mask[c] &= ~page->byte_dirty_mask[c];
                page->byte_dirty_mask[c] = 0;
        }
        if (remove_from_evict_list)
                page_remove_from_evict_list(page);
}

void codegen_block_init(uint32_t phys_addr)
{
        codeblock_t *block;
        page_t *page = &pages[phys_addr >> 12];

        if (!page->block)
                mem_flush_write_page(phys_addr, cs+cpu_state.pc);
        block = block_free_list_get();
#ifndef RELEASE_BUILD
        if (!block)
                fatal("codegen_block_init: block_free_list_get() returned NULL\n");
#endif
        block_current = get_block_nr(block);

        block_num = HASH(phys_addr);
        codeblock_hash[block_num] = block_current;

        block->ins = 0;
        block->pc = cs + cpu_state.pc;
        block->_cs = cs;
        block->phys = phys_addr;
        block->dirty_mask = &page->dirty_mask;
        block->dirty_mask2 = NULL;
        block->next = block->prev = BLOCK_INVALID;
        block->next_2 = block->prev_2 = BLOCK_INVALID;
        block->page_mask = block->page_mask2 = 0;
        block->flags = CODEBLOCK_STATIC_TOP;
        block->status = cpu_cur_status;

        recomp_page = block->phys & ~0xfff;
        codeblock_tree_add(block);
}

static ir_data_t *ir_data;

ir_data_t *codegen_get_ir_data()
{
        return ir_data;
}

void codegen_block_start_recompile(codeblock_t *block)
{
        page_t *page = &pages[block->phys >> 12];

        if (!page->block)
                mem_flush_write_page(block->phys, cs+cpu_state.pc);

        block_num = HASH(block->phys);
        block_current = get_block_nr(block);//block->pnt;

#ifndef RELEASE_BUILD
        if (block->pc != cs + cpu_state.pc || (block->flags & CODEBLOCK_WAS_RECOMPILED))
                fatal("Recompile to used block!\n");
#endif

        block->head_mem_block = codegen_allocator_allocate(NULL, block_current);
        block->data = codeblock_allocator_get_ptr(block->head_mem_block);

        block->status = cpu_cur_status;

        block->page_mask = block->page_mask2 = 0;
        block->ins = 0;

        cpu_block_end = 0;

        last_op32 = -1;
        last_ea_seg = NULL;
        last_ssegs = -1;

        codegen_block_cycles = 0;
        codegen_timing_block_start();

        codegen_block_ins = 0;
        codegen_block_full_ins = 0;

        recomp_page = block->phys & ~0xfff;

        codegen_flags_changed = 0;
        codegen_fpu_entered = 0;
        codegen_mmx_entered = 0;

        codegen_fpu_loaded_iq[0] = codegen_fpu_loaded_iq[1] = codegen_fpu_loaded_iq[2] = codegen_fpu_loaded_iq[3] =
        codegen_fpu_loaded_iq[4] = codegen_fpu_loaded_iq[5] = codegen_fpu_loaded_iq[6] = codegen_fpu_loaded_iq[7] = 0;

        cpu_state.seg_ds.checked = cpu_state.seg_es.checked = cpu_state.seg_fs.checked = cpu_state.seg_gs.checked = (cr0 & 1) ? 0 : 1;

        block->TOP = cpu_state.TOP & 7;
        block->flags |= CODEBLOCK_WAS_RECOMPILED;

        codegen_flat_ds = !(cpu_cur_status & CPU_STATUS_NOTFLATDS);
        codegen_flat_ss = !(cpu_cur_status & CPU_STATUS_NOTFLATSS);

        if (block->flags & CODEBLOCK_BYTE_MASK)
        {
                block->dirty_mask = &page->byte_dirty_mask[(block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK];
                block->dirty_mask2 = NULL;
        }

        ir_data = codegen_ir_init();
        ir_data->block = block;
        codegen_reg_reset();
        codegen_accumulate_reset();
        codegen_generate_reset();
}


void codegen_block_remove()
{
        codeblock_t *block = &codeblock[block_current];

        delete_block(block);

        recomp_page = -1;
}

void codegen_block_generate_end_mask_recompile()
{
        codeblock_t *block = &codeblock[block_current];
        page_t *p;

        p = &pages[block->phys >> 12];
        if (block->flags & CODEBLOCK_BYTE_MASK)
        {
                int offset = (block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

                p->byte_code_present_mask[offset] |= block->page_mask;
        }
        else
                p->code_present_mask |= block->page_mask;

        if ((*(block->dirty_mask) & block->page_mask) && !page_in_evict_list(p))
                page_add_to_evict_list(p);

        block->phys_2 = -1;
        block->next_2 = block->prev_2 = BLOCK_INVALID;
        if (block->page_mask2)
        {
                block->phys_2 = get_phys_noabrt(codegen_endpc);
                if (block->phys_2 != -1)
                {
                        page_t *page_2 = &pages[block->phys_2 >> 12];

                        if (block->flags & CODEBLOCK_BYTE_MASK)
                        {
                                int offset = (block->phys_2 >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

                                page_2->byte_code_present_mask[offset] |= block->page_mask2;
                                block->dirty_mask2 = &page_2->byte_dirty_mask[offset];
                        }
                        else
                        {
                                page_2->code_present_mask |= block->page_mask2;
                                block->dirty_mask2 = &page_2->dirty_mask;
                        }
                        if (((*block->dirty_mask2) & block->page_mask2) && !page_in_evict_list(page_2))
                                page_add_to_evict_list(page_2);

                        if (!pages[block->phys_2 >> 12].block_2)
                                mem_flush_write_page(block->phys_2, codegen_endpc);

#ifndef RELEASE_BUILD
                        if (!block->page_mask2)
                                fatal("!page_mask2\n");
                        if (block->next_2)
                        {
                                if (codeblock[block->next_2].pc == BLOCK_PC_INVALID)
                                        fatal("block->next_2->pc=BLOCK_PC_INVALID %p\n", (void *)&codeblock[block->next_2]);
                        }
#endif
                }
                else
                {
                        /*Second page not present. page_mask2 is most likely set only because
                          the recompiler didn't know how long the last instruction was, so
                          clear it*/
                        block->page_mask2 = 0;
                }
        }

        recomp_page = -1;
}

void codegen_block_generate_end_mask_mark()
{
        codeblock_t *block = &codeblock[block_current];
        uint32_t start_pc;
        uint32_t end_pc;
        page_t *p;

#ifndef RELEASE_BUILD
        if (block->flags & CODEBLOCK_BYTE_MASK)
                fatal("codegen_block_generate_end_mask2() - BYTE_MASK\n");
#endif

        block->page_mask = 0;
        start_pc = (block->pc & 0xfff) & ~63;
        if ((block->pc ^ codegen_endpc) & ~0xfff)
                end_pc = 0xfff & ~63;
        else
                end_pc = (codegen_endpc & 0xfff) & ~63;
        if (end_pc < start_pc)
                end_pc = 0xfff;
        start_pc >>= PAGE_MASK_SHIFT;
        end_pc >>= PAGE_MASK_SHIFT;

        for (; start_pc <= end_pc; start_pc++)
        {
                block->page_mask |= ((uint64_t)1 << start_pc);
        }

        p = &pages[block->phys >> 12];
        p->code_present_mask |= block->page_mask;
        if ((p->dirty_mask & block->page_mask) && !page_in_evict_list(p))
                page_add_to_evict_list(p);

        block->phys_2 = -1;
        block->page_mask2 = 0;
        block->next_2 = block->prev_2 = BLOCK_INVALID;
        if ((block->pc ^ codegen_endpc) & ~0xfff)
        {
                block->phys_2 = get_phys_noabrt(codegen_endpc);
                if (block->phys_2 != -1)
                {
                        page_t *page_2 = &pages[block->phys_2 >> 12];

                        start_pc = 0;
                        end_pc = (codegen_endpc & 0xfff) >> PAGE_MASK_SHIFT;
                        for (; start_pc <= end_pc; start_pc++)
                                block->page_mask2 |= ((uint64_t)1 << start_pc);

                        page_2->code_present_mask |= block->page_mask2;
                        if ((page_2->dirty_mask & block->page_mask2) && !page_in_evict_list(page_2))
                                page_add_to_evict_list(page_2);

                        if (!pages[block->phys_2 >> 12].block_2)
                                mem_flush_write_page(block->phys_2, codegen_endpc);

#ifndef RELEASE_BUILD
                        if (!block->page_mask2)
                                fatal("!page_mask2\n");
                        if (block->next_2)
                        {
                                if (codeblock[block->next_2].pc == BLOCK_PC_INVALID)
                                        fatal("block->next_2->pc=BLOCK_PC_INVALID %p\n", (void *)&codeblock[block->next_2]);
                        }
#endif
                        block->dirty_mask2 = &page_2->dirty_mask;
                }
                else
                {
                        /*Second page not present. page_mask2 is most likely set only because
                          the recompiler didn't know how long the last instruction was, so
                          clear it*/
                        block->page_mask2 = 0;
                }
        }

        recomp_page = -1;
}

void codegen_block_end()
{
        codeblock_t *block = &codeblock[block_current];

        codegen_block_generate_end_mask_mark();
        add_to_block_list(block);
}

void codegen_block_end_recompile(codeblock_t *block)
{
        codegen_timing_block_end();
        codegen_accumulate(ir_data, ACCREG_cycles, -codegen_block_cycles);

        if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
                block_dirty_list_remove(block);
        else
                remove_from_block_list(block, block->pc);
        block->next = block->prev = BLOCK_INVALID;
        block->next_2 = block->prev_2 = BLOCK_INVALID;
        codegen_block_generate_end_mask_recompile();
        add_to_block_list(block);

        if (!(block->flags & CODEBLOCK_HAS_FPU))
                block->flags &= ~CODEBLOCK_STATIC_TOP;

        codegen_accumulate_flush(ir_data);
        codegen_ir_compile(ir_data, block);
}

void codegen_flush()
{
        return;
}

void codegen_mark_code_present_multibyte(codeblock_t *block, uint32_t start_pc, int len)
{
        if (len)
        {
                uint32_t end_pc = start_pc + (len-1);

                if (block->flags & CODEBLOCK_BYTE_MASK)
                {
			uint32_t start_pc_masked = start_pc & PAGE_MASK_MASK;
			uint32_t end_pc_masked = end_pc & PAGE_MASK_MASK;

                        if ((start_pc ^ block->pc) & ~0x3f) /*Starts in second page*/
                        {
                                for (; start_pc_masked <= end_pc_masked; start_pc_masked++)
                                        block->page_mask2 |= ((uint64_t)1 << start_pc_masked);
                        }
                        else if (((start_pc + (len-1)) ^ block->pc) & ~0x3f) /*Crosses both pages*/
                        {
                                for (; start_pc_masked <= 63; start_pc_masked++)
                                        block->page_mask |= ((uint64_t)1 << start_pc_masked);
                                for (start_pc_masked = 0; start_pc_masked <= end_pc_masked; start_pc_masked++)
                                        block->page_mask2 |= ((uint64_t)1 << start_pc_masked);
                        }
                        else /*First page only*/
                        {
                                for (; start_pc_masked <= end_pc_masked; start_pc_masked++)
                                        block->page_mask |= ((uint64_t)1 << start_pc_masked);
                        }
                }
                else
                {
                        uint32_t start_pc_shifted = start_pc >> PAGE_MASK_SHIFT;
                        uint32_t end_pc_shifted = end_pc >> PAGE_MASK_SHIFT;
                        start_pc_shifted &= PAGE_MASK_MASK;
                        end_pc_shifted &= PAGE_MASK_MASK;

                        if ((start_pc ^ block->pc) & ~0xfff) /*Starts in second page*/
                        {
                                for (; start_pc_shifted <= end_pc_shifted; start_pc_shifted++)
                                        block->page_mask2 |= ((uint64_t)1 << start_pc_shifted);
                        }
                        else if (((start_pc + (len-1)) ^ block->pc) & ~0xfff) /*Crosses both pages*/
                        {
                                for (; start_pc_shifted <= 63; start_pc_shifted++)
                                        block->page_mask |= ((uint64_t)1 << start_pc_shifted);
                                for (start_pc_shifted = 0; start_pc_shifted <= end_pc_shifted; start_pc_shifted++)
                                        block->page_mask2 |= ((uint64_t)1 << start_pc_shifted);
                        }
                        else /*First page only*/
                        {
                                for (; start_pc_shifted <= end_pc_shifted; start_pc_shifted++)
                                        block->page_mask |= ((uint64_t)1 << start_pc_shifted);
                        }
                }
        }
}
