#ifndef _CODEGEN_H_
#define _CODEGEN_H_

#include <86box/mem.h>
#include "codegen_public.h"
#include <stddef.h>
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

typedef struct codeblock_t {
    uint32_t pc;
    uint32_t _cs;
    uint32_t phys, phys_2;
    uint16_t status;
    uint16_t flags;
    uint8_t  ins;
    uint8_t  TOP;
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__loongarch_lp64)
    /* ARM64/LoongArch64: per-block retry counter used to delay
       NO_IMMEDIATES promotion until churn repeats, reducing premature
       slow-immediate mode. (The 386_dynarec.c policy consuming these
       fields is currently ARM64-only; LoongArch64 wires it up in M5.) */
    uint8_t  dirty_list_recompile_hits;
    /* ARM64/LoongArch64: tracks the most recent dirty-list epoch seen by
       this block so promotion can require a dense burst, not stale
       spaced-out hits. */
    uint16_t dirty_list_last_epoch;
#endif
    int      valid;

    /*Pointers for codeblock tree, used to search for blocks when hash lookup
      fails.*/
    uint32_t parent, left, right;

    uint8_t *data;

    uint64_t  page_mask, page_mask2;
    uint64_t *dirty_mask, *dirty_mask2;

    /*Previous and next pointers, for the codeblock list associated with
      each physical page. Two sets of pointers, as a codeblock can be
      present in two pages.*/
    uint32_t prev, next;
    uint32_t prev_2, next_2;

    /*First mem_block_t used by this block. Any subsequent mem_block_ts
      will be in the list starting at head_mem_block->next.*/
    struct mem_block_t *head_mem_block;

    /*Run since eviction's sweep last passed it: the sweep passes it over
      once (codegen_delete_random_block).*/
    uint8_t used;

#ifdef CODEGEN_CHAIN
    /*Where a linked block enters it (data[chain_entry]): past the part of
      the prologue every block shares, so a chain keeps one frame.*/
    uint16_t chain_entry;

    /*Links into this block hold while this is the value they were made
      under: anything that changes what they reach (its code written, the
      block deleted, its page's translation changed) adds one. Never reset
      while the block's number is in use again, so a stale link cannot
      match a new block.*/
    uint64_t link_gen;
    /*The block's pages as translated when a link to it was last made
      (mmu_link_translate): a translation flush compares them again.*/
    uint64_t link_pte, link_pte2;
    /*While it is a link target: its group (codegen_block.c), or the solo
      list, and its neighbours there.*/
    int32_t link_group;
    int32_t link_gnext, link_gprev;
    /*A link to it was made at CPL 3: its pages' user bit counts.*/
    uint8_t link_cpl3;
    /*The physical pages counted for it while it is a link target.*/
    uint32_t link_phys, link_phys_2;
    /*Its places in the lists of linked blocks on those pages (codegen_lp),
      one per page: 0 for the first, 1 for the second.*/
    int32_t lp_next[2], lp_prev[2];
#endif
} codeblock_t;

#ifdef CODEGEN_CHAIN
extern uint64_t codegen_chain_gen;
extern uint8_t *codegen_chain_last_exit;
extern int      codegen_end_link_ok;
extern int      codegen_end_link_pending;
extern int      codegen_block_full;
/*A link stub found the signal word set: answers 0 for the chain to go on,
  non-zero to leave it (386_dynarec.c).*/
extern int codegen_chain_service(void);
/*STI in a block: an interrupt already pending breaks the chain, at the end
  of the block STI's shadow ends (386_dynarec.c).*/
extern void codegen_chain_sti(void);
/*What a link or an indirect entry was made under and holds only while it
  matches: the status word, and the CPL (bits 17:16) -- a page's user or
  supervisor check was made at the CPL a link was made at, so privilege
  changes need not drop the links.*/
#    define CODEGEN_CHAIN_KEY() ((cpu_cur_status & 0xffff) | ((uint32_t) (cpu_state.seg_cs.access & 0x60) << 11))
/*A link generation no block has: where an unlinked stub or an empty
  indirect entry points.*/
extern uint64_t codegen_link_unlinked;
/*The indirect link table: the blocks a near RET, CALL or indirect JMP last
  reached, looked up by linear PC in the exit stub, each valid for the CS
  base and key it was filled under and while its block's link generation
  is the one it was filled under.*/
#    define CODEGEN_IND_SIZE 32768
typedef struct codegen_ind_t {
    uint32_t  pc;
    uint32_t  cs_base;
    uint32_t  status; /* CODEGEN_CHAIN_KEY() */
    uint32_t  pad;
    uint64_t  gen;      /* the block's link_gen when filled */
    uint8_t  *target;
    uint64_t *link_gen; /* &block->link_gen, or &codegen_link_unlinked */
    uint64_t *group_word; /* its group's word, as a link's (codegen_link_group_word) */
    uint64_t  group_value;
    uint64_t  spare;
} codegen_ind_t; /* 64 bytes: the stub indexes it with SHL 6 */
extern codegen_ind_t codegen_ind_cache[CODEGEN_IND_SIZE];
extern uint32_t      codegen_ind_miss;
extern void          codegen_ind_fill(codeblock_t *block, uint64_t gen);
/*A link's second test: the word of the target's link group and the value
  it must hold (codegen_block.c).*/
extern uint64_t  codegen_lg_solo_word;
extern uint64_t *codegen_link_group_word(const codeblock_t *block, uint64_t *value);
/*Point a link stub at a block, under the block's link generation `gen`
  (read before its code was found clean); answers whether it could.*/
extern int codegen_chain_patch(uint8_t *stub, codeblock_t *target, uint64_t gen);

/*Links into a block, kept exact (codegen_block.c):
  - note: a link or indirect entry to `block` was just made;
  - drop: every link into `block` goes (from anywhere);
  - drop_all: every link goes (`owner`: from the dispatcher's side, which
    also empties the link lists; else from anywhere, generations only);
  - revalidate_request: translations may have changed (all of them, or the
    linear page `lin` alone); the links are checked against the page
    tables at the loop's next look (codegen_links_revalidate).*/
extern void codegen_links_note(codeblock_t *block);
extern void codegen_links_drop(codeblock_t *block);
extern void codegen_links_drop_all(int owner);
extern void codegen_links_revalidate_request(int all, uint32_t lin);
extern void codegen_links_revalidate(void);
/*Compiled code was written: the links into the written blocks go, at the
  loop's next look.*/
extern void codegen_links_written(void);
#endif

extern codeblock_t *codeblock;

extern uint32_t *codeblock_hash;

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

#define BLOCK_PC_INVALID        0xffffffff

#define BLOCK_INVALID           0

static inline int
get_block_nr(codeblock_t *block)
{
    return ((uintptr_t) block - (uintptr_t) codeblock) / sizeof(codeblock_t);
}

static inline codeblock_t *
codeblock_tree_find(uint32_t phys, uint32_t _cs)
{
    codeblock_t *block;
    uint64_t     a = _cs | ((uint64_t) phys << 32);

    if (!pages[phys >> 12].head)
        return NULL;

    block = &codeblock[pages[phys >> 12].head];
    while (block) {
        uint64_t block_cmp = block->_cs | ((uint64_t) block->phys << 32);
        if (a == block_cmp) {
            if (!((block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) && ((block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK)))
                break;
        }
        if (a < block_cmp)
            block = block->left ? &codeblock[block->left] : NULL;
        else
            block = block->right ? &codeblock[block->right] : NULL;
    }

    return block;
}

static inline void
codeblock_tree_add(codeblock_t *new_block)
{
    codeblock_t *block = &codeblock[pages[new_block->phys >> 12].head];
    uint64_t     a     = new_block->_cs | ((uint64_t) new_block->phys << 32);

    if (!pages[new_block->phys >> 12].head) {
        pages[new_block->phys >> 12].head = get_block_nr(new_block);
        new_block->parent = new_block->left = new_block->right = BLOCK_INVALID;
    } else {
        codeblock_t *old_block     = NULL;
        uint64_t     old_block_cmp = 0;

        while (block) {
            old_block     = block;
            old_block_cmp = old_block->_cs | ((uint64_t) old_block->phys << 32);

            if (a < old_block_cmp)
                block = block->left ? &codeblock[block->left] : NULL;
            else
                block = block->right ? &codeblock[block->right] : NULL;
        }

        if (old_block != NULL) {
            if (a < old_block_cmp)
                old_block->left = get_block_nr(new_block);
            else
                old_block->right = get_block_nr(new_block);

            new_block->parent = get_block_nr(old_block);
        } else
            new_block->parent = BLOCK_INVALID;

        new_block->left = new_block->right = BLOCK_INVALID;
    }
}

static inline void
codeblock_tree_delete(codeblock_t *block)
{
    uint32_t     parent_nr = block->parent;
    codeblock_t *parent;

    if (block->parent)
        parent = &codeblock[block->parent];
    else
        parent = NULL;

    if (!block->left && !block->right) {
        /*Easy case - remove from parent*/
        if (!parent)
            pages[block->phys >> 12].head = BLOCK_INVALID;
        else {
            uint32_t block_nr = get_block_nr(block);

            if (parent->left == block_nr)
                parent->left = BLOCK_INVALID;
            if (parent->right == block_nr)
                parent->right = BLOCK_INVALID;
        }
        return;
    } else if (!block->left) {
        /*Only right node*/
        if (!parent_nr) {
            pages[block->phys >> 12].head                   = block->right;
            codeblock[pages[block->phys >> 12].head].parent = BLOCK_INVALID;
        } else {
            uint32_t block_nr = get_block_nr(block);

            if (parent->left == block_nr) {
                parent->left                   = block->right;
                codeblock[parent->left].parent = parent_nr;
            }
            if (parent->right == block_nr) {
                parent->right                   = block->right;
                codeblock[parent->right].parent = parent_nr;
            }
        }
        return;
    } else if (!block->right) {
        /*Only left node*/
        if (!parent_nr) {
            pages[block->phys >> 12].head                   = block->left;
            codeblock[pages[block->phys >> 12].head].parent = BLOCK_INVALID;
        } else {
            uint32_t block_nr = get_block_nr(block);

            if (parent->left == block_nr) {
                parent->left                   = block->left;
                codeblock[parent->left].parent = parent_nr;
            }
            if (parent->right == block_nr) {
                parent->right                   = block->left;
                codeblock[parent->right].parent = parent_nr;
            }
        }
        return;
    } else {
        /*Difficult case - node has two children. Walk right child to find lowest node*/
        codeblock_t *lowest = &codeblock[block->right];
        codeblock_t *highest;
        codeblock_t *old_parent;
        uint32_t     lowest_nr;

        while (lowest->left)
            lowest = &codeblock[lowest->left];
        lowest_nr = get_block_nr(lowest);

        old_parent = &codeblock[lowest->parent];

        /*Replace deleted node with lowest node*/
        if (!parent_nr)
            pages[block->phys >> 12].head = lowest_nr;
        else {
            uint32_t block_nr = get_block_nr(block);

            if (parent->left == block_nr)
                parent->left = lowest_nr;
            if (parent->right == block_nr)
                parent->right = lowest_nr;
        }

        lowest->parent = parent_nr;
        lowest->left   = block->left;
        if (lowest->left)
            codeblock[lowest->left].parent = lowest_nr;

        old_parent->left = BLOCK_INVALID;

        highest = &codeblock[lowest->right];
        if (!lowest->right) {
            if (lowest_nr != block->right) {
                lowest->right                  = block->right;
                codeblock[block->right].parent = lowest_nr;
            }
            return;
        }

        while (highest->right)
            highest = &codeblock[highest->right];

        if (block->right && block->right != lowest_nr) {
            highest->right                 = block->right;
            codeblock[block->right].parent = get_block_nr(highest);
        }
    }
}

#define PAGE_MASK_MASK  63
#define PAGE_MASK_SHIFT 6

void codegen_mark_code_present_multibyte(codeblock_t *block, uint32_t start_pc, int len);

static inline void
codegen_mark_code_present(codeblock_t *block, uint32_t start_pc, int len)
{
    if (len == 1) {
        if (block->flags & CODEBLOCK_BYTE_MASK) {
            if (!((start_pc ^ block->pc) & ~0x3f)) /*Starts in second page*/
                block->page_mask |= ((uint64_t) 1 << (start_pc & PAGE_MASK_MASK));
            else
                block->page_mask2 |= ((uint64_t) 1 << (start_pc & PAGE_MASK_MASK));
        } else {
            if (!((start_pc ^ block->pc) & ~0xfff)) /*Starts in second page*/
                block->page_mask |= ((uint64_t) 1 << ((start_pc >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK));
            else
                block->page_mask2 |= ((uint64_t) 1 << ((start_pc >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK));
        }
    } else
        codegen_mark_code_present_multibyte(block, start_pc, len);
}

extern void codegen_init(void);
extern void codegen_reset(void);
extern void codegen_block_init(uint32_t phys_addr);
extern void codegen_block_remove(void);
extern void codegen_block_start_recompile(codeblock_t *block);
extern void codegen_block_end_recompile(codeblock_t *block);
extern void codegen_block_end(void);
extern void codegen_delete_block(codeblock_t *block);
extern void codegen_generate_call(uint8_t opcode, OpFn op, uint32_t fetchdat, uint32_t new_pc, uint32_t old_pc);
extern void codegen_generate_seg_restore(void);
extern void codegen_set_op32(void);
extern void codegen_flush(void);
extern void codegen_check_flush(struct page_t *page, uint64_t mask, uint32_t phys_addr);
struct ir_data_t;
x86seg     *codegen_generate_ea(struct ir_data_t *ir, x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, uint32_t op_32, int stack_offset);
extern void codegen_check_seg_read(codeblock_t *block, struct ir_data_t *ir, x86seg *seg);
extern void codegen_check_seg_write(codeblock_t *block, struct ir_data_t *ir, x86seg *seg);
extern void codegen_check_regs(void);

extern int codegen_purge_purgable_list(void);
/*Delete a random code block to free memory. This is obviously quite expensive, and
  will only be called when the allocator is out of memory*/
extern void codegen_delete_random_block(int required_mem_block);

extern int      cpu_block_end;
extern uint32_t codegen_endpc;

extern int cpu_reps;
extern int cpu_notreps;

extern int codegen_block_cycles;

extern void (*codegen_timing_start)(void);
extern void (*codegen_timing_prefix)(uint8_t prefix, uint32_t fetchdat);
extern void (*codegen_timing_opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
extern void (*codegen_timing_block_start)(void);
extern void (*codegen_timing_block_end)(void);
extern int (*codegen_timing_jump_cycles)(void);

typedef struct codegen_timing_t {
    void (*start)(void);
    void (*prefix)(uint8_t prefix, uint32_t fetchdat);
    void (*opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
    void (*block_start)(void);
    void (*block_end)(void);
    int (*jump_cycles)(void);
} codegen_timing_t;

extern codegen_timing_t codegen_timing_pentium;
extern codegen_timing_t codegen_timing_686;
extern codegen_timing_t codegen_timing_486;
extern codegen_timing_t codegen_timing_winchip;
extern codegen_timing_t codegen_timing_winchip2;
extern codegen_timing_t codegen_timing_k5;
extern codegen_timing_t codegen_timing_k6;
extern codegen_timing_t codegen_timing_p6;

void codegen_timing_set(codegen_timing_t *timing);

extern int block_current;
extern int block_pos;

#define CPU_BLOCK_END() cpu_block_end = 1

/*Current physical page of block being recompiled. -1 if no recompilation taking place */
extern uint32_t recomp_page;

extern x86seg  *op_ea_seg;
extern int      op_ssegs;
extern uint32_t op_old_pc;

/*Set to 1 if flags have been changed in the block being recompiled, and hence
  flags_op is known and can be relied on */
extern int codegen_flags_changed;

extern int codegen_fpu_entered;
extern int codegen_mmx_entered;

extern int codegen_fpu_loaded_iq[8];
extern int codegen_reg_loaded[8];

extern int codegen_in_recompile;

void codegen_generate_reset(void);

int  codegen_get_instruction_uop(codeblock_t *block, uint32_t pc, int *first_instruction, int *TOP);
void codegen_set_loop_start(struct ir_data_t *ir, int first_instruction);

#ifdef DEBUG_EXTRA
extern uint32_t instr_counts[256 * 256];
#endif

#endif
