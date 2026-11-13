#ifndef _CODEGEN_H_
#define _CODEGEN_H_

#include <86box/mem.h>
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
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__amd64__) || defined(_M_X64)
    /*1 + index of the codegen_link_mcache record filled for this block, 0
      if none. See codegen_link.h.*/
    uint16_t link_mcache;
    /*Changes whenever the block is freed, recompiled or has a field a link
      slot relies on changed; never 0. See codegen_link.h.*/
    uint32_t link_gen;
#endif
    int      valid;

    /*Pointers for codeblock tree, used to search for blocks when hash lookup
      fails.*/
    uint32_t parent, left, right;

    /*Run since eviction's sweep last passed it: the sweep passes it over
      once (codegen_delete_random_block).*/
    uint8_t used;

    /*Self-modifying-code thrash tracking: each time this block is compiled
      and then rewritten before it paid for itself (fewer than
      SMC_THRASH_MIN_RUNS runs, see runs_left), smc_strikes goes up and,
      past a few, arms smc_cooldown_until (a tsc deadline). Until then the
      dynarec does not compile the block again but interprets it, since
      compiling code that is rewritten before it can be reused enough just
      adds cost with no payback. A compile that *did* pay for itself resets
      smc_strikes to 0. Kept per block, so code that is rewritten often does
      not keep other code on its page from compiling.*/
    uint8_t smc_strikes;

    /*Chunk list of its first page the block is on (codegen_block.c,
      page_blocks_t).*/
    uint8_t page_chunk;

    /*How often this block's code was recently rewritten after fewer than
      SMC_FAMILY_MIN_RUNS runs (codegen_smc_policy.h). Only a block rewritten
      that often goes on to CODEBLOCK_NO_OPCODES, whose code costs more per
      run than a block compiled for the bytes it has.*/
    uint8_t smc_family_score;

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

    /*One bit per run from block->data: starts all ones when compiled and
      each dispatcher or linked entry shifts it right, saturating at zero
      without a branch, so it has run at least n times once it is at most
      UINT64_MAX >> n (codeblock_ran()). The compile pass itself does not
      count as a run.*/
    uint64_t runs_left;
    uint64_t smc_cooldown_until;
} codeblock_t;

/*Keep the cutoff below the measured break-even point for fragmented SMC
  workloads: a block that ran this often is cheaper to compile than to
  repeatedly interpret, even if shorter blocks could benefit for longer.*/
#define SMC_THRASH_MIN_RUNS 8

/*runs_left of a block just compiled; of one whose compile is paid for by
  its next run (an unrolled loop); and of one already paid for (a kept
  version put back)*/
#define CODEBLOCK_RUNS_NEW      UINT64_MAX
#define CODEBLOCK_RUNS_UNROLLED (UINT64_MAX >> (SMC_THRASH_MIN_RUNS - 1))
#define CODEBLOCK_RUNS_PAID     (UINT64_MAX >> SMC_THRASH_MIN_RUNS)

/*Whether a block with this runs_left ran at least n (1 to 64) times*/
static inline int
codeblock_ran(uint64_t runs_left, unsigned n)
{
    return (n >= 64) ? (runs_left == 0) : (runs_left <= (UINT64_MAX >> n));
}

/*Most guest instructions a block is compiled from.*/
#define MAX_INSTRUCTION_COUNT 50

extern codeblock_t *codeblock;

extern uint32_t *codeblock_hash;

extern uint8_t *block_write_data;

extern int     codegen_timing_peek;
extern uint8_t codegen_peekb(uint32_t addr);


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
/*Code block is on the block list of its first page*/
#define CODEBLOCK_IN_PAGE_LIST 0x100
/*Code block (also CODEBLOCK_BYTE_MASK and CODEBLOCK_NO_IMMEDIATES) reads the
  opcode of each unprefixed ALU EAX,imm32 instruction (ADD, OR, ADC, SBB, AND,
  SUB, XOR, CMP) from memory as it runs, and does the one found there: code
  that rewrites which of them it does need not be compiled again. An opcode
  out of the family leaves the block to be compiled again*/
#define CODEBLOCK_NO_OPCODES 0x200
/*The code compiled for the block reads an opcode as it runs
  (CODEBLOCK_NO_OPCODES), so it can leave the block on a miss*/
#define CODEBLOCK_OPCODE_FAMILY 0x400
/*Code block is on the dirty list of blocks with kept versions
  (with CODEBLOCK_IN_DIRTY_LIST)*/
#define CODEBLOCK_DIRTY_KEPT 0x800
/*Code block off the dirty list has been held in the SMC cooldown once since
  it was put there, and already moved up a compile mode (with
  CODEBLOCK_IN_DIRTY_LIST)*/
#define CODEBLOCK_REVIVE_HELD 0x1000

#define BLOCK_PC_INVALID        0xffffffff

#define BLOCK_INVALID           0

static inline int
get_block_nr(const codeblock_t *block)
{
    return ((uintptr_t) block - (uintptr_t) codeblock) / sizeof(codeblock_t);
}

/*Blocks are keyed by CS base and physical address, but blocks with the same
  key can still differ in their linear address (the same physical code mapped
  at another address in another process, such as a shared library).
  codeblock_tree_add() puts each later one to the right of an equal key, so
  the walk has to go on past a key that matches until the linear address
  matches too. Stopping at the first one never found the others: every
  lookup that missed the hash slot then compiled the code again.*/
static inline codeblock_t *
codeblock_tree_find(uint32_t phys, uint32_t _cs, uint32_t pc)
{
    codeblock_t *block;
    uint64_t     a = _cs | ((uint64_t) phys << 32);

    if (!pages[phys >> 12].head)
        return NULL;

    block = &codeblock[pages[phys >> 12].head];
    while (block) {
        uint64_t block_cmp = block->_cs | ((uint64_t) block->phys << 32);
        if (a == block_cmp) {
            if ((block->pc == pc) && !((block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) && ((block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK)))
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

/*The second 4 KB page's walk (dynarec_fetch_walk), emitted once per compile
  where the interpreter's fetch first reads from that page: at the front of
  the instruction whose own fetch reaches it, else where a handler takes a
  byte from it (codegen_fetch_walk_mark). codegen_fetch_walk_off: the marks
  of bytes that fetch covers, which do not walk. codegen_fetch_walk_defer:
  the marks of an EA whose walk is made later, by a hand-over's op in its
  fetch_ea or after a compiled x87 instruction's address is formed; the
  address is kept in codegen_fetch_walk_deferred with bit 0 set, 0 for
  none. Such a walk is past the instruction's front, where the bytes before
  it are on the first page (a page-2 byte among those the front fetch reads
  walks there), so its address is always a page's first byte, linear 0
  included, and the low bits are free*/
extern int      codegen_page2_walked;
extern int      codegen_fetch_walk_off;
extern int      codegen_fetch_walk_defer;
extern uint32_t codegen_fetch_walk_deferred;
extern void     codegen_fetch_walk(uint32_t addr);
extern int      dynarec_fetch_walk(uint32_t addr);

/*The walk for bytes a handler takes, where it is called. The interpreter's
  op fetches an immediate after the EA and the segment's null and writable
  test, before the operand (IMUL and TEST r/m,imm read the operand first),
  so a handler that marks it after the access calls this there. In a block
  that reads its immediates from RAM (CODEBLOCK_NO_IMMEDIATES) an immediate
  not wholly on the first page is still marked (immediate_off_first_page)*/
static inline void
codegen_fetch_walk_mark(codeblock_t *block, uint32_t start_pc, int len)
{
    if (!codegen_page2_walked && !codegen_fetch_walk_off && (((start_pc + len - 1) ^ block->pc) & ~0xfff))
        codegen_fetch_walk(((start_pc ^ block->pc) & ~0xfff) ? start_pc : ((start_pc + len - 1) & ~0xfff));
}

static inline void
codegen_mark_code_present(codeblock_t *block, uint32_t start_pc, int len)
{
    codegen_fetch_walk_mark(block, start_pc, len);
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
extern int  codegen_block_restore_version(codeblock_t *block);
extern int  codegen_block_dirty_patched(const codeblock_t *block);
extern void codegen_block_version_check_ins(uint32_t addr);
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
/*Check up to max pages of the evict list for blocks written over*/
extern void codegen_flush_evict_list(int max);
/*Pages exec386_dynarec() checks after each run of the CPU (about 1 ms)*/
#define CODEGEN_EVICT_FLUSH_MAX 64
/*Delete a random code block to free memory. This is obviously quite expensive, and
  will only be called when the allocator is out of memory*/
extern void codegen_delete_random_block(int required_mem_block);

extern int      cpu_block_end;
extern uint32_t codegen_endpc;
/*Opcode bytes of the block being compiled that its code reads as it runs
  (CODEBLOCK_OPCODE_FAMILY): bit n of word c is byte 64 * c + n from the
  start of the block's first 64-byte chunk*/
extern uint64_t codegen_family_ops[2];

extern int cpu_reps;
extern int cpu_notreps;

extern int codegen_block_cycles;

extern void (*codegen_timing_start)(void);
extern void (*codegen_timing_prefix)(uint8_t prefix, uint32_t fetchdat);
extern void (*codegen_timing_opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
extern void (*codegen_timing_block_start)(void);
extern void (*codegen_timing_block_end)(void);
extern int (*codegen_timing_jump_cycles)(void);

/*What a model's block start keeps from the block before*/
typedef struct codegen_timing_state_t {
    uint32_t regmask_modified;
    int      fpu_latency;
    int      fpu_st_latency[8];
} codegen_timing_state_t;

extern void (*codegen_timing_save)(codegen_timing_state_t *state);
extern void (*codegen_timing_load)(const codegen_timing_state_t *state);

typedef struct codegen_timing_t {
    void (*start)(void);
    void (*prefix)(uint8_t prefix, uint32_t fetchdat);
    void (*opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
    void (*block_start)(void);
    void (*block_end)(void);
    int (*jump_cycles)(void);
    /*save copies the kept state out, load sets it, or clears it when given
      NULL. NULL where block start clears it all*/
    void (*save)(codegen_timing_state_t *state);
    void (*load)(const codegen_timing_state_t *state);
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
/*Forgets the block's inline flags rebuilds (codegen_ops_helpers.c)*/
extern void codegen_flags_known_reset(void);
/*An inline flags rebuild of the block: its uops start..end-1 and the bits of
  flags it writes*/
typedef struct codegen_flags_inline_t {
    int      start;
    int      end;
    uint16_t written;
} codegen_flags_inline_t;
extern int codegen_flags_inline_sites(const codegen_flags_inline_t **sites);
/*A flag read compiled as one uop in place of the one uop calling func: the
  bits of flags the call reads as the dead flags pass sees it, and the uop
  that set the flags op it was compiled for when an unrolled copy whose loop
  starts after that uop must call func instead (-1 otherwise). pair: the uop
  writes 16 bits of its register and the next uop, a CMP_IMM_JZ/JNZ_DEST,
  tests them; a copy that calls func tests the whole result instead*/
typedef struct codegen_flags_read_t {
    int      uop;
    uint16_t read;
    int      producer;
    void    *func;
    int      pair;
} codegen_flags_read_t;
extern int codegen_flags_read_sites(const codegen_flags_read_t **sites);
/*Called by a CODEBLOCK_NO_OPCODES block that found an opcode out of the
  family it reads at run time: the block is compiled again when next run. 1*/
extern int codegen_opcode_family_miss(uint32_t block_nr);
extern int  family_review_count;
extern void codegen_family_review(void);

/*Set while the block being recompiled is a V86 block entered with every
  segment limit 0..0xffff (CPU_STATUS_NOT64K clear): V86 segment loads keep
  them so, an instruction run by the interpreter may not*/
extern int codegen_v86_64k;
/*The first uop of the instruction being recompiled*/
extern int codegen_instr_first_uop;

extern int codegen_fpu_entered;
/*Set when a compiled instruction since the block's last pending exception
  check can have set ES and gone on: the next waiting x87 instruction checks
  again although the block has entered the FPU*/
extern int codegen_fpu_es_check;
extern int codegen_mmx_entered;

extern int codegen_fpu_loaded_iq[8];
extern int codegen_reg_loaded[8];

extern int codegen_in_recompile;

void codegen_generate_reset(void);

int  codegen_get_instruction_uop(codeblock_t *block, uint32_t pc, int *first_instruction, int *TOP);
void codegen_get_instruction_entered(int ins, int *fpu_entered, int *fpu_es_check, int *mmx_entered);
void codegen_get_instruction_tags(int ins, uint8_t *tag_valid, uint8_t *tag_empty);
void codegen_set_loop_start(struct ir_data_t *ir, int first_instruction);

#ifdef DEBUG_EXTRA
extern uint32_t instr_counts[256 * 256];
#endif

/*Compiled far transfers: the x86seg routine and the globals around it
  (386_dynarec_ops.c)*/
extern void dynarec_iret_pm(int is32);
extern void dynarec_iret_cs(uint16_t seg);
extern int  dynarec_dtr(uint32_t op);
extern void dynarec_lmsw(uint16_t tempw);
extern int  dynarec_call_far_w(uint16_t new_cs, uint32_t ret_pc);
extern int  dynarec_call_far_l(uint16_t new_cs, uint32_t ret_pc);

/*The interpreted loop's RF handling after an instruction, with the 486
  debug registers option (386_dynarec_ops.c)*/
extern void dynarec_rf_after_op(void);

/*Compiled MOV CRx and MOV DRx: the control register writes with what they
  flush, and the debug register accesses (386_dynarec_ops.c)*/
extern int  dynarec_mov_cr0(uint32_t rm);
extern void dynarec_mov_cr2(uint32_t rm);
extern int  dynarec_mov_cr3(uint32_t rm);
extern int  dynarec_mov_cr4(uint32_t rm);
extern int  dynarec_mov_drx_gd(void);
extern int  dynarec_mov_drx(uint32_t op);

/*A compiled MOV SS and the instruction after it (codegen.c)*/
extern int      codegen_mov_ss_pair_valid;
extern uint32_t codegen_mov_ss_pair_pc;
extern int      codegen_instruction_room(void);

#endif
