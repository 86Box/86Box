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

/*Instructions of a block's mark kept for its interpreted runs
  (codeblock_t::dec): what fills a codeblock_t to 256 bytes on arm64*/
#define CODEBLOCK_DEC_NR 24
/*A new block is interpreted for this many runs after its mark before it is
  compiled: most code run in a burst of new code (a boot, a program
  starting) runs only a few times, and a compile costs about as much as a
  hundred interpreted runs of the same block. A long block gets fewer, as
  many as CODEGEN_WARM_INSNS instructions take: each of its runs costs that
  much more interpreted, so code that does run on pays less for waiting*/
#define CODEGEN_WARM_RUNS  63
#define CODEGEN_WARM_INSNS 1024

typedef struct codeblock_t {
    /*What a linked entry reads and writes of the block it goes to, in the
      first 32 bytes: with the block's size a multiple of 32 (the alignment)
      they are one cache line, the one the link routines prefetch.
      Changes whenever the block is freed, recompiled or has a field a link
      slot relies on changed; never 0. See codegen_link.h.*/
#if defined(__aarch64__) || defined(_M_ARM64)
    /*256 bytes a block on arm64, so the link routines find one from its
      number with a shift*/
    _Alignas(256) uint32_t link_gen;
#else
    _Alignas(32) uint32_t link_gen;
#endif
    /*Run since eviction's sweep last passed it: the sweep passes it over
      once (codegen_delete_random_block).*/
    uint8_t used;
    /*One bit per run from block->data still needed to pay for compiling.
      Each dispatcher or linked entry shifts right, saturating at zero
      without a branch. The compile pass itself does not count as a run.
      SMC_THRASH_MIN_RUNS bits: 16 on arm64; a byte elsewhere, right after
      used, as the x86-64 link hop loads the two as one word*/
#if defined(__aarch64__) || defined(_M_ARM64)
    uint16_t runs_left;
#else
    uint8_t runs_left;
#endif
    uint64_t  page_mask2;
    uint64_t *dirty_mask2;

    uint32_t pc;
    uint32_t _cs;
    uint32_t phys, phys_2;
    /*An address on the block's second page (page_mask2), whose translation
      is checked against phys_2: the next page, or with
      CODEGEN_BACKEND_HAS_FAR_PAGE2 any page the recording went on to*/
    uint32_t pc_2;
    uint16_t status;
    uint16_t flags;
    uint8_t  ins;
    uint8_t  TOP;
    /*Runs still to be interpreted before the block is compiled
      (CODEGEN_WARM_RUNS)*/
    uint8_t  warm_left;
    /*Entries in dec[]*/
    uint8_t  dec_n;
    int      valid;

    /*Pointers for codeblock tree, used to search for blocks when hash lookup
      fails.*/
    uint32_t parent, left, right;

    uint8_t *data;

    uint64_t  page_mask;
    uint64_t *dirty_mask;

    /*Previous and next pointers, for the codeblock list associated with
      each physical page. Two sets of pointers, as a codeblock can be
      present in two pages.*/
    uint32_t prev, next;
    uint32_t prev_2, next_2;

    /*First mem_block_t used by this block. Any subsequent mem_block_ts
      will be in the list starting at head_mem_block->next.*/
    struct mem_block_t *head_mem_block;

    /*Self-modifying-code thrash tracking: each time this block is compiled
      and then rewritten before it paid for itself (runs_left still not 0),
      smc_strikes goes up and, past a few, arms smc_cooldown_until (a tsc
      deadline). Until then the dynarec does not compile the block again but
      interprets it, since compiling code that is rewritten before it can be
      reused enough just adds cost with no payback. A compile that *did* pay
      for itself resets smc_strikes to 0. Kept per block, so code that is
      rewritten often does not keep other code on its page from compiling.*/
    uint8_t  smc_strikes;
    /*Versions swapped in since the block was last compiled
      (codegen_block_swap_version()), up to 255*/
    uint8_t  smc_swaps;
    /*1 + the codegen_poly[] entry the block runs through, 0 = none*/
    uint16_t poly;
    /*Set by the block's start when a CODEBLOCK_ALU_FAMILY opcode is no
      longer one of the family: it left without running anything*/
    uint8_t  family_miss;
    /*The slot of the block's end in codegen_link_slots[block_nr][]*/
    uint8_t  end_slot;
    /*Searches of codegen_block_swap_version() in a row that found no kept
      version: past SWAP_MISS_MAX it stops searching until swap_off_until*/
    uint8_t  swap_misses;
    uint64_t smc_cooldown_until;

    /*tsc when the block was last compiled. A byte-mask block not rewritten
      for SMC_BYTE_MASK_DECAY after that is compiled again as an ordinary
      one: rewrites made it one, and it would stay one, its immediates read
      from memory and its loops not unrolled, for as long as it lives.*/
    uint64_t compiled_tsc;
    /*codegen_block_chunk_sum() of its bytes when compiled_tsc was last set:
      a byte-mask block whose chunks changed since is still being rewritten,
      through the bytes it reads as it runs, and is not made ordinary yet*/
    uint32_t smc_sum;
    /*The bytes of the block's first two 64-byte chunks written since the
      block was made (codegen_block_note_written()): a compile reads only
      those of its immediates and family opcodes as it runs, and compiles the
      rest in as code*/
    uint64_t smc_written[2];
    /*A CODEBLOCK_FAMILY_RETIME block: the kinds of its retimed family slots
      (codegen_family_retime()) its cycles were last timed for, by the
      compile family_serial names, and what that adds to the cycles the
      code charges*/
    uint32_t family_serial;
    int32_t  family_delta;
    uint64_t family_key;
    int32_t  family_delta_end;
    /*tsc at its last family miss: CODEBLOCK_FAMILY_RETIME and _NOFIX are
      dropped at a compile long after it, the code there likely another's*/
    uint64_t family_miss_tsc;
    /*tsc until which codegen_block_swap_version() does not search*/
    uint64_t swap_off_until;
    /*The instructions its mark ran, for its interpreted runs
      (exec386_dynarec_int_dec()): each one's first byte, and its length in
      the top bits, 0 for one that went elsewhere or was the last*/
    uint16_t dec[CODEBLOCK_DEC_NR];
} codeblock_t;

#define SMC_BYTE_MASK_DECAY (1ull << 21)
/*How far before a block's start its recording may follow a Jcc back to a
  loop's head (codegen_follow_jcc())*/
#define CODEGEN_JCC_BACK 512
extern uint32_t codegen_block_chunk_sum(const codeblock_t *block);
extern void     codegen_block_note_written(codeblock_t *block);
extern int      codegen_family_beyond_ok(const codeblock_t *block, uint32_t addr);
/*1 if a byte from addr on (in the block's first two chunks, the second one
  on the next page too) was ever written*/
static inline int
codegen_block_written(const codeblock_t *block, uint32_t addr, int len)
{
    const uint32_t first = addr - (block->pc & ~0x3f);
    const uint32_t last  = first + (len - 1);

    if (last >= 128)
        return 1;
    for (uint32_t c = first; c <= last; c++) {
        if (block->smc_written[c >> 6] & ((uint64_t) 1 << (c & 63)))
            return 1;
    }
    return 0;
}

/*Backends with CODEGEN_BACKEND_HAS_POLY: a one-chunk byte-mask block that keeps being rewritten between a few
  versions of its code runs through a trampoline instead
  (codegen_poly_tramp[]): a routine compares the chunk's bytes with each
  version's and goes to the one that matches, or back to the C loop with
  miss set. Its page_mask is 0 meanwhile, so writes to the chunk do not
  invalidate it; the comparison is what makes it safe to run.*/
#    define CODEGEN_POLY_NR       64
#    define CODEGEN_POLY_VERSIONS 6
typedef struct codegen_poly_version_t {
    uint64_t m[16];    /*The version's code bytes in the chunk and the next one on
                         its page, as a byte mask per word*/
    uint64_t c[16];    /*Their values*/
    uint8_t *entry;    /*Its link entry*/
    uint64_t two;      /*Nonzero if it has code in the next chunk*/
} codegen_poly_version_t;
typedef struct codegen_poly_t {
    uint8_t      **mem_exec;   /*&_mem_exec[] of the chunk's page*/
    const uint8_t *exec;       /*What it held when the versions were compiled*/
    const uint8_t *code;       /*The chunk, through exec*/
    uint8_t      **mem_exec_2; /*The same for the next chunk, on this page or the next one*/
    const uint8_t *exec_2;
    const uint8_t *code_2;     /*64 bytes before that chunk, through exec_2*/
    uint32_t       nr;       /*Versions, 0 = entry free*/
    uint32_t       miss;     /*Set by the routine when none matched*/
    codegen_poly_version_t v[CODEGEN_POLY_VERSIONS];
} codegen_poly_t;
extern codegen_poly_t codegen_poly[CODEGEN_POLY_NR];
extern void          *codegen_poly_tramp[CODEGEN_POLY_NR];
extern void           codegen_poly_drop(codeblock_t *block);

/*The opcodes of a CODEBLOCK_ALU_FAMILY block's family instructions, through
  their host pointers, as the block being compiled has them so far*/
#define CODEGEN_ALU_FAMILY_MAX 64
extern int            codegen_alu_family_nr;
extern const uint8_t *codegen_alu_family_ptr[CODEGEN_ALU_FAMILY_MAX];
/*And their physical addresses*/
extern uint32_t       codegen_alu_family_addr[CODEGEN_ALU_FAMILY_MAX];
/*The block's instruction number of the last family instruction compiled*/
extern int            codegen_alu_family_prev_ins;
/*Whether it took ADC and SBB, and so left its carry out in cpu_state.alu_family_cf*/
extern int            codegen_alu_family_prev_carry;
/*Each one's kind: 2 the ops that write EAX, 3 CMP, 4 ADC and SBB (in carry
  mode), checked against codegen_alu_family_bad_kind[kind - 2]; 5 retimed,
  taking all eight; 6 fixed to its one op (UOP_ALU_FAMILY_FIXED)*/
extern uint8_t        codegen_alu_family_carry[CODEGEN_ALU_FAMILY_MAX];
/*Each one's opcode as compiled, for a slot compiled to that op alone
  (codegen_alu_family_carry[] 6, UOP_ALU_FAMILY_FIXED)*/
extern uint8_t        codegen_alu_family_op[CODEGEN_ALU_FAMILY_MAX];
/*0 for the family's eight opcodes, 1 for any other*/
extern uint8_t        codegen_alu_family_bad_c[256];
extern uint8_t        codegen_alu_family_bad_kind[3][256];
extern void           codegen_alu_family_init(void);
/*Set by the arm64 backend for a block that gave up its fixed family slots,
  for codegen_alu_family_miss() to leave it unretimed*/
extern codeblock_t   *codegen_family_nofix_block;
extern void           codegen_alu_family_miss(codeblock_t *block);
/*A slot's kind as a retimed slot keeps it in the block's key: 0
  for the opcodes that write EAX, 1 for CMP, 2 for ADC and SBB, 0x80 for any
  other (codegen_alu_family_carry[] 5)*/
extern uint8_t        codegen_alu_family_kind[256];
/*The bit of the key a retimed slot's kind goes to*/
extern uint8_t        codegen_alu_family_key_shift[CODEGEN_ALU_FAMILY_MAX];
/*Most slots one block retimes: two bits each in a 64-bit key*/
#define CODEGEN_FAMILY_RETIME_SLOTS 32
struct ir_data_t;
extern int            codegen_family_retime(codeblock_t *block, uint64_t key, uint32_t serial);
extern void           codegen_family_retime_begin(codeblock_t *block);
extern void           codegen_family_retime_end(void);
extern void           codegen_family_retime_slot(int slot);
extern int            codegen_family_retime_on(void);
extern void           codegen_family_retime_ins(struct ir_data_t *ir, uint8_t opcode);
extern void           codegen_family_retime_lump(struct ir_data_t *ir, codeblock_t *block, int site_ok);

/*Keep the cutoff below the measured break-even point for fragmented SMC
  workloads: a block that ran this often is cheaper to compile than to
  repeatedly interpret, even if shorter blocks could benefit for longer.
  Compiling costs more next to interpreting on arm64, where a block that
  runs 8 to 16 times between rewrites (the int_smc_rate_op tests) ran up to
  1.8 times faster interpreted.*/
#if defined(__aarch64__) || defined(_M_ARM64)
#    define SMC_THRASH_MIN_RUNS 16
#else
#    define SMC_THRASH_MIN_RUNS 8
#endif
#define SMC_THRASH_RUN_MASK ((1u << SMC_THRASH_MIN_RUNS) - 1)

#if SMC_THRASH_MIN_RUNS < 1 || SMC_THRASH_MIN_RUNS > 16
#    error SMC reuse tracking must fit in 16 bits
#endif

/*Most guest instructions a block is compiled from.*/
#define MAX_INSTRUCTION_COUNT 100

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
/*Rewritten after it read its immediates from RAM: ADD, OR, AND, SUB, XOR and
  CMP EAX,imm32, one form with one timing on every model, read their opcode
  from RAM too and do what it says, and the block's start checks that each
  of those opcodes is still one of the six
  (backends with CODEGEN_BACKEND_HAS_ALU_FAMILY)*/
#define CODEBLOCK_ALU_FAMILY 0x200
/*An ADC or SBB EAX,imm32 was compiled in a CODEBLOCK_ALU_FAMILY block (one
  in carry mode, codegen_alu_family_eax_imm())*/
#define CODEBLOCK_ALU_FAMILY_C 0x400
/*The block's code calls C (CALL_FUNC and the like), which may change CS or
  the CPL: its exits store the loop-top oldcs and oldcpl as they link
  (codegen_link_patch()). Set for a version swapped or restored in.*/
#define CODEBLOCK_CALLS_C 0x800
/*A CODEBLOCK_ALU_FAMILY block that a slot changing kind left: its
  family instructions are compiled to take all eight opcodes, and a new mix
  of kinds is timed as the block runs (codegen_family_retime()) rather than
  compiled again*/
#define CODEBLOCK_FAMILY_RETIME 0x1000
/*The block's code may change IF or TF (POPF, CLI, STI, IRET) without
  calling C, which the link routine checks between two blocks with the
  pending events. Set for a version swapped or restored in, as
  CODEBLOCK_CALLS_C is.*/
#define CODEBLOCK_LINK_STATE 0x2000
/*A CODEBLOCK_ALU_FAMILY block whose fixed slots (UOP_ALU_FAMILY_FIXED) were
  rewritten too often for the runs between: compiled with every slot reading
  its opcode as it runs from then on*/
#define CODEBLOCK_FAMILY_NOFIX 0x4000
/*The block's code has fixed family slots: a version kept with them is not
  taken back once the block is CODEBLOCK_FAMILY_NOFIX*/
#define CODEBLOCK_FAMILY_FIXED_CODE 0x8000
/*Set by an instruction compiled into the block that sets CODEBLOCK_LINK_STATE*/
extern int     codegen_link_state_ins;

#define BLOCK_PC_INVALID        0xffffffff

#define BLOCK_INVALID           0

static inline int
get_block_nr(codeblock_t *block)
{
    return ((uintptr_t) block - (uintptr_t) codeblock) / sizeof(codeblock_t);
}

/*Blocks with the same physical address can still differ in their linear
  address (the same physical code mapped at another address in another
  process, such as a shared library), and a lookup must find the one at the
  linear address it is after: stopping at the first block of that physical
  address never found the others, and every lookup that missed the hash slot
  then compiled the code again. So the linear address is part of a block's
  key in its page's tree, with the physical address, and a guest that places
  its libraries at random does not line them up in one long chain of equal
  keys. Blocks are mostly made at rising addresses, which would also grow the
  tree into a chain: the key is scrambled by a bijection (an odd multiplier)
  so that it stays shallow. Only finding a block needs the order.*/
static inline uint64_t
codeblock_tree_key(uint32_t phys, uint32_t pc)
{
    return (pc | ((uint64_t) phys << 32)) * 0x9e3779b97f4a7c15ULL;
}

static inline codeblock_t *
codeblock_tree_find(uint32_t phys, uint32_t _cs, uint32_t pc)
{
    codeblock_t *block;
    uint64_t     a = codeblock_tree_key(phys, pc);

    if (!pages[phys >> 12].head)
        return NULL;

    block = &codeblock[pages[phys >> 12].head];
    while (block) {
        uint64_t block_cmp = codeblock_tree_key(block->phys, block->pc);
        if ((a == block_cmp) && (block->_cs == _cs)) {
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
    uint64_t     a     = codeblock_tree_key(new_block->phys, new_block->pc);

    if (!pages[new_block->phys >> 12].head) {
        pages[new_block->phys >> 12].head = get_block_nr(new_block);
        new_block->parent = new_block->left = new_block->right = BLOCK_INVALID;
    } else {
        codeblock_t *old_block     = NULL;
        uint64_t     old_block_cmp = 0;

        while (block) {
            old_block     = block;
            old_block_cmp = codeblock_tree_key(old_block->phys, old_block->pc);

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
extern int  codegen_block_swap_version(codeblock_t *block, struct page_t *page, uint32_t phys_addr);
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
/*codegen_purge_stale_page(): at most once in this many cycles*/
#define CODEGEN_PURGE_INTERVAL (1ull << 18)
extern uint64_t codegen_purge_tsc;
extern void     codegen_purge_stale_page(void);
/*Delete a random code block to free memory. This is obviously quite expensive, and
  will only be called when the allocator is out of memory*/
extern void codegen_delete_random_block(int required_mem_block);

extern int      cpu_block_end;
/*codegen_flags_dead()*/
extern int codegen_flags_debt_ins;
extern int codegen_flags_dead(codeblock_t *block, uint32_t addr);
extern int codegen_ea_extra_len(uint32_t fetchdat, uint32_t op_32);
extern int codegen_reg_versions_left(void);
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
/*Blocks compiled with the caches off (codegen.c)*/
extern int  codegen_nocache;
extern void codegen_nocache_begin(void);
extern void codegen_nocache_end(void);
/*The cycles an instruction just recorded took (386_dynarec.c)*/
extern void codegen_nocache_ins_cycles(int cycles_used);
extern codegen_timing_t *codegen_timing_current;

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
extern int codegen_followed_near;
extern int  codegen_follow_jcc(uint32_t next_pc, uint32_t dest_addr);
/*An instruction at linear addr may go into the block being recorded (on
  one of its two pages); set takes a second page if there is none yet
  (CODEGEN_BACKEND_HAS_FAR_PAGE2)*/
extern int  codegen_record_page_ok(uint32_t addr, int set);
extern int  codegen_record_page_held(uint32_t addr);
extern void codegen_follow_reset(void);

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
extern void dynarec_invlpg(uint32_t addr);
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
