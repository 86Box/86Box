#ifndef _CODEGEN_H_
#define _CODEGEN_H_

#include "mem.h"
#include "x86_ops.h"

/*Handling self-modifying code (of which there is a lot on x86) :

  PCem tracks a 'dirty mask' for each physical page, in which each bit
  represents 64 bytes. This is only tracked for pages that have code in - when a
  page first has a codeblock generated, it is evicted from the writelookup and
  added to the page_lookup for this purpose. When in the page_lookup, each write
  will go through the mem_write_ram*_page() functions and set the dirty mask
  appropriately.
  
  Each codeblock also contains a code mask (actually two masks, one for each
  page the block is/may be in), again with each bit representing 64 bytes.
  
  Each page has a list of codeblocks present in it. As each codeblock can span
  up to two pages, two lists are present.
  
  When a codeblock is about to be executed, the code masks are compared with the
  dirty masks for the relevant pages. If either intersect, then
  codegen_check_flush() is called on the affected page(s), and all affected
  blocks are evicted.
  
  The 64 byte granularity appears to work reasonably well for most cases,
  avoiding most unnecessary evictions (eg when code & data are stored in the
  same page).
*/

typedef struct codeblock_t
{
        uint32_t pc;
        uint32_t _cs;
        uint32_t phys, phys_2;
        uint16_t status;
        uint16_t flags;
        uint8_t ins;
        uint8_t TOP;

        /*Pointers for codeblock tree, used to search for blocks when hash lookup
          fails.*/
        uint16_t parent, left, right;

        uint8_t *data;
        
        uint64_t page_mask, page_mask2;
        uint64_t *dirty_mask, *dirty_mask2;

        /*Previous and next pointers, for the codeblock list associated with
          each physical page. Two sets of pointers, as a codeblock can be
          present in two pages.*/
        uint16_t prev, next;
        uint16_t prev_2, next_2;

        /*First mem_block_t used by this block. Any subsequent mem_block_ts
          will be in the list starting at head_mem_block->next.*/
        struct mem_block_t *head_mem_block;
} codeblock_t;

extern codeblock_t *codeblock;

extern uint16_t *codeblock_hash;

extern uint8_t *block_write_data;

/*Code block uses FPU*/
#define CODEBLOCK_HAS_FPU 1
/*Code block is always entered with the same FPU top-of-stack*/
#define CODEBLOCK_STATIC_TOP 2
/*Code block has been compiled*/
#define CODEBLOCK_WAS_RECOMPILED 4
/*Code block is in free list and is not valid*/
#define CODEBLOCK_IN_FREE_LIST 8
/*Code block spans two pages, page_mask2 and dirty_mask2 are valid*/
#define CODEBLOCK_HAS_PAGE2 0x10
/*Code block is using a byte mask for code present and dirty*/
#define CODEBLOCK_BYTE_MASK 0x20
/*Code block is in dirty list*/
#define CODEBLOCK_IN_DIRTY_LIST 0x40
/*Code block is not inlining immediate parameters, parameters must be fetched from memory*/
#define CODEBLOCK_NO_IMMEDIATES 0x80

#define BLOCK_PC_INVALID 0xffffffff

#define BLOCK_INVALID 0

static inline int get_block_nr(codeblock_t *block)
{
        return ((uintptr_t)block - (uintptr_t)codeblock) / sizeof(codeblock_t);
}

static inline codeblock_t *codeblock_tree_find(uint32_t phys, uint32_t _cs)
{
        codeblock_t *block;
        uint64_t a = _cs | ((uint64_t)phys << 32);
        
        if (!pages[phys >> 12].head)
                return NULL;
                
        block = &codeblock[pages[phys >> 12].head];
        while (block)
        {
                uint64_t block_cmp = block->_cs | ((uint64_t)block->phys << 32);
                if (a == block_cmp)
                {
                        if (!((block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) &&
                             ((block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK)))
                                break;
                }
                if (a < block_cmp)
                        block = block->left ? &codeblock[block->left] : NULL;
                else
                        block = block->right ? &codeblock[block->right] : NULL;
        }
        
        return block;
}

static inline void codeblock_tree_add(codeblock_t *new_block)
{
        codeblock_t *block = &codeblock[pages[new_block->phys >> 12].head];
        uint64_t a = new_block->_cs | ((uint64_t)new_block->phys << 32);

        if (!pages[new_block->phys >> 12].head)
        {
                pages[new_block->phys >> 12].head = get_block_nr(new_block);
                new_block->parent = new_block->left = new_block->right = BLOCK_INVALID;
        }
        else
        {
                codeblock_t *old_block = NULL;
                uint64_t old_block_cmp = 0;
                
                while (block)
                {
                        old_block = block;
                        old_block_cmp = old_block->_cs | ((uint64_t)old_block->phys << 32);
                        
                        if (a < old_block_cmp)
                                block = block->left ? &codeblock[block->left] : NULL;
                        else
                                block = block->right ? &codeblock[block->right] : NULL;
                }
                
                if (a < old_block_cmp)
                        old_block->left = get_block_nr(new_block);
                else
                        old_block->right = get_block_nr(new_block);
                
                new_block->parent = get_block_nr(old_block);
                new_block->left = new_block->right = BLOCK_INVALID;
        }
}

static inline void codeblock_tree_delete(codeblock_t *block)
{
        uint16_t parent_nr = block->parent;
        codeblock_t *parent;

        if (block->parent)
                parent = &codeblock[block->parent];
        else
                parent = NULL;

        if (!block->left && !block->right)
        {
                /*Easy case - remove from parent*/
                if (!parent)
                        pages[block->phys >> 12].head = BLOCK_INVALID;
                else
                {
                        uint16_t block_nr = get_block_nr(block);
                        
                        if (parent->left == block_nr)
                                parent->left = BLOCK_INVALID;
                        if (parent->right == block_nr)
                                parent->right = BLOCK_INVALID;
                }
                return;
        }
        else if (!block->left)
        {
                /*Only right node*/
                if (!parent_nr)
                {
                        pages[block->phys >> 12].head = block->right;
                        codeblock[pages[block->phys >> 12].head].parent = BLOCK_INVALID;
                }
                else
                {
                        uint16_t block_nr = get_block_nr(block);

                        if (parent->left == block_nr)
                        {
                                parent->left = block->right;
                                codeblock[parent->left].parent = parent_nr;
                        }
                        if (parent->right == block_nr)
                        {
                                parent->right = block->right;
                                codeblock[parent->right].parent = parent_nr;
                        }
                }
                return;
        }
        else if (!block->right)
        {
                /*Only left node*/
                if (!parent_nr)
                {
                        pages[block->phys >> 12].head = block->left;
                        codeblock[pages[block->phys >> 12].head].parent = BLOCK_INVALID;
                }
                else
                {
                        uint16_t block_nr = get_block_nr(block);

                        if (parent->left == block_nr)
                        {
                                parent->left = block->left;
                                codeblock[parent->left].parent = parent_nr;
                        }
                        if (parent->right == block_nr)
                        {
                                parent->right = block->left;
                                codeblock[parent->right].parent = parent_nr;
                        }
                }
                return;
        }
        else
        {
                /*Difficult case - node has two children. Walk right child to find lowest node*/
                codeblock_t *lowest = &codeblock[block->right], *highest;
                codeblock_t *old_parent;
                uint16_t lowest_nr;
                        
                while (lowest->left)
                        lowest = &codeblock[lowest->left];
                lowest_nr = get_block_nr(lowest);
                
                old_parent = &codeblock[lowest->parent];

                /*Replace deleted node with lowest node*/
                if (!parent_nr)
                        pages[block->phys >> 12].head = lowest_nr;
                else
                {
                        uint16_t block_nr = get_block_nr(block);

                        if (parent->left == block_nr)
                                parent->left = lowest_nr;
                        if (parent->right == block_nr)
                                parent->right = lowest_nr;
                }

                lowest->parent = parent_nr;
                lowest->left = block->left;
                if (lowest->left)
                        codeblock[lowest->left].parent = lowest_nr;

                old_parent->left = BLOCK_INVALID;
                                
                highest = &codeblock[lowest->right];
                if (!lowest->right)
                {
                        if (lowest_nr != block->right)
                        {
                                lowest->right = block->right;
                                codeblock[block->right].parent = lowest_nr;
                        }
                        return;
                }

                while (highest->right)
                        highest = &codeblock[highest->right];

                if (block->right && block->right != lowest_nr)
                {
                        highest->right = block->right;
                        codeblock[block->right].parent = get_block_nr(highest);
                }
        }
}

#define PAGE_MASK_MASK 63
#define PAGE_MASK_SHIFT 6

void codegen_mark_code_present_multibyte(codeblock_t *block, uint32_t start_pc, int len);

static inline void codegen_mark_code_present(codeblock_t *block, uint32_t start_pc, int len)
{
        if (len == 1)
        {
                if (block->flags & CODEBLOCK_BYTE_MASK)
                {
                        if (!((start_pc ^ block->pc) & ~0x3f)) /*Starts in second page*/
                                block->page_mask |= ((uint64_t)1 << (start_pc & PAGE_MASK_MASK));
                        else
                                block->page_mask2 |= ((uint64_t)1 << (start_pc & PAGE_MASK_MASK));
                }
                else
                {
                        if (!((start_pc ^ block->pc) & ~0xfff)) /*Starts in second page*/
                                block->page_mask |= ((uint64_t)1 << ((start_pc >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK));
                        else
                                block->page_mask2 |= ((uint64_t)1 << ((start_pc >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK));
                }
        }
        else
                codegen_mark_code_present_multibyte(block, start_pc, len);
}

void codegen_init();
void codegen_close();
void codegen_reset();
void codegen_block_init(uint32_t phys_addr);
void codegen_block_remove();
void codegen_block_start_recompile(codeblock_t *block);
void codegen_block_end_recompile(codeblock_t *block);
void codegen_block_end();
void codegen_delete_block(codeblock_t *block);
void codegen_generate_call(uint8_t opcode, OpFn op, uint32_t fetchdat, uint32_t new_pc, uint32_t old_pc);
void codegen_generate_seg_restore();
void codegen_set_op32();
void codegen_flush();
void codegen_check_flush(struct page_t *page, uint64_t mask, uint32_t phys_addr);
struct ir_data_t;
x86seg *codegen_generate_ea(struct ir_data_t *ir, x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, uint32_t op_32, int stack_offset);
void codegen_check_seg_read(codeblock_t *block, struct ir_data_t *ir, x86seg *seg);
void codegen_check_seg_write(codeblock_t *block, struct ir_data_t *ir, x86seg *seg);

int codegen_purge_purgable_list();
/*Delete a random code block to free memory. This is obviously quite expensive, and
  will only be called when the allocator is out of memory*/
void codegen_delete_random_block(int required_mem_block);

extern int cpu_block_end;
extern uint32_t codegen_endpc;

extern int cpu_recomp_blocks, cpu_recomp_full_ins, cpu_new_blocks;
extern int cpu_recomp_blocks_latched, cpu_recomp_ins_latched, cpu_recomp_full_ins_latched, cpu_new_blocks_latched;
extern int cpu_recomp_flushes, cpu_recomp_flushes_latched;
extern int cpu_recomp_evicted, cpu_recomp_evicted_latched;
extern int cpu_recomp_reuse, cpu_recomp_reuse_latched;
extern int cpu_recomp_removed, cpu_recomp_removed_latched;

extern int cpu_reps, cpu_reps_latched;
extern int cpu_notreps, cpu_notreps_latched;

extern int codegen_block_cycles;

extern void (*codegen_timing_start)();
extern void (*codegen_timing_prefix)(uint8_t prefix, uint32_t fetchdat);
extern void (*codegen_timing_opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
extern void (*codegen_timing_block_start)();
extern void (*codegen_timing_block_end)();
extern int (*codegen_timing_jump_cycles)();

typedef struct codegen_timing_t
{
        void (*start)();
        void (*prefix)(uint8_t prefix, uint32_t fetchdat);
        void (*opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
        void (*block_start)();
        void (*block_end)();
        int (*jump_cycles)();
} codegen_timing_t;

extern codegen_timing_t codegen_timing_pentium;
extern codegen_timing_t codegen_timing_686;
extern codegen_timing_t codegen_timing_486;
extern codegen_timing_t codegen_timing_winchip;
extern codegen_timing_t codegen_timing_winchip2;
extern codegen_timing_t codegen_timing_k6;

void codegen_timing_set(codegen_timing_t *timing);

extern int block_current;
extern int block_pos;

#define CPU_BLOCK_END() cpu_block_end = 1

/*Current physical page of block being recompiled. -1 if no recompilation taking place */
extern uint32_t recomp_page;

extern x86seg *op_ea_seg;
extern int op_ssegs;
extern uint32_t op_old_pc;

/*Set to 1 if flags have been changed in the block being recompiled, and hence
  flags_op is known and can be relied on */
extern int codegen_flags_changed;

extern int codegen_fpu_entered;
extern int codegen_mmx_entered;

extern int codegen_fpu_loaded_iq[8];
extern int codegen_reg_loaded[8];

extern int codegen_in_recompile;

void codegen_generate_reset();

int codegen_get_instruction_uop(codeblock_t *block, uint32_t pc, int *first_instruction, int *TOP);
void codegen_set_loop_start(struct ir_data_t *ir, int first_instruction);

#ifdef DEBUG_EXTRA
extern uint32_t instr_counts[256*256];
#endif

#endif
