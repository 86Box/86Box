#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>
#include <86box/timer.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x87_sf.h"
#include "x87.h"

#include "386_common.h"

#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_link.h"
#include "codegen_public.h"
#include "codegen_reg.h"
#include "codegen_smc_policy.h"

extern int codegen_record_faulted; /* 386_common.c */

uint8_t *block_write_data = NULL;

int      codegen_flat_ds;
int      codegen_flat_ss;
int      codegen_wide_ds;
int      codegen_wide_ss;
int      codegen_v86_64k;
int      mmx_ebx_ecx_loaded;
int      codegen_flags_changed = 0;
int      codegen_fpu_entered   = 0;
int      codegen_fpu_es_check  = 0;
int      codegen_mmx_entered   = 0;
uint8_t  codegen_fpu_tag_valid = 0;
uint8_t  codegen_fpu_tag_empty = 0;
int      codegen_fpu_loaded_iq[8];
x86seg  *op_ea_seg;
int      op_ssegs;
uint32_t op_old_pc;

uint32_t recomp_page = -1;
/*The page of a block's second chunk or page while the block is compiled,
  once code there is compiled: writes to it take the tracked path as well
  (addwritelookup()), so that a store into bytes already compiled from it
  marks them dirty, as on the first page. -1 when there is none.*/
uint32_t recomp_page2 = -1;

int        block_current = 0;
static int block_num;
int        block_pos;

uint32_t codegen_endpc;

int        codegen_block_cycles;

#ifdef DEBUG_EXTRA
uint32_t instr_counts[256 * 256];
#endif

static uint32_t block_free_list;
static void     delete_block(codeblock_t *block);
static void     delete_dirty_block(codeblock_t *block);

/*Temporary lists of code blocks that have recently been evicted. This allows
  for some historical state to be kept when a block is the target of
  self-modifying code.

  A block that has kept versions (see below) goes on the second list, any
  other on the first. Each list is limited to dirty_list_max[] blocks; when
  that is exceeded the oldest entry of that list is moved to the free list.
  A kept version is put back for the cost of comparing its bytes, so such a
  block is worth keeping much longer: code that switches between a few
  contents, each rewritten long before it runs again (a table of call thunks
  repointed one by one), would lose its versions if it fell off a short list.
  A block with none would only be compiled again at once when next run, where
  one made afresh gets the page's cooldown and a marking pass first; those
  keep the short list. For the same reason a block off either list that has
  no version to put back gets the page's cooldown as a new block would,
  unless its code was only patched (codegen_block_dirty_patched()).*/
enum {
    DIRTY_LIST_PLAIN = 0,
    DIRTY_LIST_KEPT,
    DIRTY_LISTS
};
static uint32_t  block_dirty_list_head[DIRTY_LISTS];
static uint32_t  block_dirty_list_tail[DIRTY_LISTS];
static int       dirty_list_size[DIRTY_LISTS];
static const int dirty_list_max[DIRTY_LISTS] = { 64, 4096 };

/*Each page's blocks also sit on one of 65 lists per page list
  (codeblock_t::chunk_key): 0-63 for a block whose dirty test reads only the
  dirty bits of that 64-byte chunk (and of the next one, for a checked
  block's first list), 64 for every other block. A page gets an entry with
  its first listed block and frees it when both lists are empty. Entry 0
  is none. mask_base is the byte dirty array the lists are keyed to.*/
#define CHUNK_LIST_OTHER 64
typedef struct block_chunk_lists_t {
    uint32_t head[2][CHUNK_LIST_OTHER + 1];
    union {
        const uint64_t *mask_base;
        uint32_t        next_free;
    };
} block_chunk_lists_t;
static uint32_t            *block_chunk_page;   /*Per page number: its entry, or 0*/
static uint32_t             block_chunk_page_nr;
static block_chunk_lists_t *block_chunk_lists;
static uint32_t             block_chunk_lists_nr, block_chunk_lists_max;
static uint32_t             block_chunk_lists_free;
static uint64_t             block_list_stamp;

/*Earlier compiled versions of byte-mask blocks that read their immediates from
  memory (CODEBLOCK_NO_IMMEDIATES). Code that flips one of its opcodes back and
  forth, such as a clc/stc used as a switch, dirties its block on every flip,
  and no compile mode covers a changed opcode. When such a block is
  invalidated its compiled code is kept with the code bytes it was compiled
  from; when the block is next run, a kept version whose bytes match guest
  memory again is put back instead of compiling the same bytes again. It is
  put back only on the terms any cached block is run on: the same code bytes,
  status and FPU top-of-stack, and the same page for its second chunk. The
  bytes are read where instructions are fetched from (_mem_exec), and that
  mapping must be the one the version was compiled under: a version reads its
  immediates through pointers into it, and ROM and shadow RAM at one address
  can differ.*/
#define BLOCK_VERSION_NR        8192
#define BLOCK_VERSION_PER_BLOCK 16

enum {
    BLOCK_VERSION_FREE = 0,
    BLOCK_VERSION_CURRENT,
    BLOCK_VERSION_KEPT
};

typedef struct block_version_t {
    struct mem_block_t *head_mem_block;
    const uint8_t      *exec, *exec_2; /*_mem_exec[] of each chunk's page when compiled*/
    uint64_t            page_mask, page_mask2;
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    /*The entry check of a version compiled for a checked block: per word of
      code compared, its bytes and the mask of the ones compiled from*/
    uint64_t            chk[2 * 16];
#endif
    uint64_t            family_ops[2]; /*codegen_family_ops when compiled: without CODEBLOCK_OPCODE_FAMILY, the opcodes it would read as it runs*/
    uint32_t            phys_2;
    uint32_t            owner; /*Number of the code block*/
    uint16_t            cs_sel; /*CS selector: x87 code stores it as the FPU CS*/
    uint16_t            status;
    uint16_t            flags; /*CODEBLOCK_HAS_FPU, CODEBLOCK_STATIC_TOP and CODEBLOCK_OPCODE_FAMILY of this version*/
    uint16_t            next;  /*Next older kept version of the owner, or the next free one*/
    uint8_t             TOP;
    uint8_t             state;
    uint8_t             checked; /*Compiled for a checked block, with the entry check*/
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    uint8_t             chk_nr;       /*Words the entry check compares*/
    uint8_t             chk_word[16]; /*Their offsets in code, in words*/
#endif
    uint8_t             code[128]; /*The first 64-byte chunk, then the second*/
} block_version_t;

/*Index 0 is not used: 0 means none*/
static block_version_t block_versions[BLOCK_VERSION_NR + 1];
static uint16_t        block_version_free_list;
static uint16_t        block_version_hand = 1;
/*Block flags that belong to a compiled version*/
#define BLOCK_VERSION_FLAGS (CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP | CODEBLOCK_OPCODE_FAMILY)

/*Records in state BLOCK_VERSION_KEPT: with none, a full pool has nothing to
  free, so block_version_alloc() need not scan it*/
static int block_version_kept_nr;
/*Version of the code a block holds now, and its newest kept version*/
static uint16_t block_version_cur[BLOCK_SIZE];
static uint16_t block_version_kept[BLOCK_SIZE];
/*The block's two chunks as fetched when its compile started. A version is
  recorded only if they are unchanged at the end: the compile pass runs the
  block, and not every change to code bytes or to the second chunk's mapping
  marks them dirty (page table accessed bits, writes through a direct write
  lookup, a CR3 load). Each instruction is also checked as it is compiled
  (codegen_block_version_check_ins()), since a change can be undone again
  before the end.*/
static int            block_version_start_ok;
static const uint8_t *block_version_start_exec, *block_version_start_exec_2;
static uint32_t       block_version_start_phys_2;
static uint16_t       block_version_start_cs;
static uint8_t        block_version_start_code[128];

codegen_entry_check_t codegen_entry_check;
/*The dirty mask a checked block is given: writes never set it*/
static uint64_t block_untracked_mask;
/*A block's chk_dirty, as its dirty pointer says: untracked, the two words its
  entry check and the link routine test, as writes set them*/
static void
block_chk_dirty_sync(codeblock_t *block)
{
    block->chk_dirty = (block->dirty_mask == &block_untracked_mask) ?
        &pages[block->phys >> 12].byte_dirty_mask[(block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK] : NULL;
}

/*A page's code or dirty masks are about to change: its fast store chunks no
  longer hold. Its byte masks may be another page's (an alias), whose own fast
  chunks read the same byte masks, so that page's go too.*/
static void
page_store_fast_clear(page_t *page)
{
    page->store_fast_mask = 0;
    page->store_full_mask = 0;
    if (!page->byte_masks_own && page->byte_dirty_mask && (page->byte_dirty_mask >= byte_dirty_mask)) {
        const uintptr_t owner = (uintptr_t) (page->byte_dirty_mask - byte_dirty_mask) / 64;

        if ((owner < pages_sz) && (pages[owner].byte_dirty_mask == page->byte_dirty_mask)) {
            pages[owner].store_fast_mask = 0;
            pages[owner].store_full_mask = 0;
        }
    }
}
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
/*The version a checked block's compile records, once its code is generated*/
static uint16_t block_version_checked_nr;
/*The cooldown a checked block's strike sets when it is compiled again where
  it is, applied as that compile starts (codegen_block_check())*/
static codeblock_t *block_checked_cooldown_block;
static uint64_t     block_checked_cooldown;
/*The checked block the loop is entering (codegen_block_check())*/
static codeblock_t *block_checked_entering;
static void         block_checked_invalidate(codeblock_t *block);

/*Whether a checked block's bytes were written since its page was last
  flushed: the dirty bits writes set, which its dirty pointers do not show*/
static int
block_checked_written(const codeblock_t *block)
{
    const uint64_t *dirty = &pages[block->phys >> 12].byte_dirty_mask[(block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK];

    return (dirty[0] & block->page_mask) || (dirty[1] & block->page_mask2);
}

/*A checked block with code, whichever kind of version it runs: written, it
  is invalidated without the dirty list (block_checked_invalidate())*/
static int
block_checked_has_code(const codeblock_t *block)
{
    return ((block->flags & (CODEBLOCK_CHECKED | CODEBLOCK_WAS_RECOMPILED)) == (CODEBLOCK_CHECKED | CODEBLOCK_WAS_RECOMPILED)) && block->head_mem_block;
}

#endif
uint64_t codegen_family_ops[2];

/*Per block: smc_family_hold() state*/
static uint8_t block_family_hold[BLOCK_SIZE];
uint32_t block_family_runs[BLOCK_SIZE];

/*A block that leaves by a loop's branch it could have gone on through
  (jcc_exit_hot) is compiled again after JCC_HOT_EXITS such exits, so the
  compile pass can see the branch taken. At most JCC_HOT_TRIES times from
  codegen_block_init(), so a branch that goes either way does not keep its
  block compiling.*/
#define JCC_HOT_EXITS 256
#define JCC_HOT_TRIES 4
static uint16_t block_jcc_hot_left[BLOCK_SIZE];
static uint8_t  block_jcc_hot_tries[BLOCK_SIZE];

/*A block whose code keeps being rewritten to bytes it held before has a kept
  version put back each time (codegen_block_restore_version()), but still
  takes a dirty marking, a flush and a restore for every rewrite. After
  SELF_CHECK_ENTER restores with no compile between them it leaves its page's
  block list (CODEBLOCK_SELF_CHECK): with no other block listed there, writes
  to the page go straight to memory. Its bytes are checked against its
  version as it is entered, and another kept version is put in place if one
  matches. After SELF_CHECK_STABLE entries in a row with the same version it
  goes back to the list, as it does when no version matches.*/
#define SELF_CHECK_ENTER  4
#define SELF_CHECK_STABLE 64
/*Restores since the last compile, or entries with the same version*/
static uint8_t block_self_check_count[BLOCK_SIZE];

/*In the compile pass: whether the exit is to be counted; arms the count*/
int
codegen_jcc_hot_can(const codeblock_t *block)
{
    const uint32_t block_nr = get_block_nr(block);

    if (block_jcc_hot_tries[block_nr] >= JCC_HOT_TRIES)
        return 0;
    block_jcc_hot_left[block_nr] = JCC_HOT_EXITS;
    return 1;
}

/*From the block's code, as it takes such an exit. Its code is not freed
  here: the dispatcher compiles the block again when it is next run.*/
int
codegen_jcc_hot_exit(uint32_t block_nr)
{
    codeblock_t *block = &codeblock[block_nr];

    if (--block_jcc_hot_left[block_nr])
        return 0;
    block_jcc_hot_tries[block_nr]++;
    if (block->flags & CODEBLOCK_WAS_RECOMPILED) {
        block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
        codegen_link_block_changed(block);
#endif
    }
    return 0;
}

static void
block_version_owner_sync(uint32_t block_nr)
{
    codegen_allocator_owner(block_nr, codeblock[block_nr].head_mem_block || block_version_kept[block_nr]);
}

static void
block_versions_init(void)
{
    memset(block_versions, 0, sizeof(block_versions));
    memset(block_version_cur, 0, sizeof(block_version_cur));
    memset(block_version_kept, 0, sizeof(block_version_kept));
    for (int c = 1; c <= BLOCK_VERSION_NR; c++)
        block_versions[c].next = (c < BLOCK_VERSION_NR) ? (c + 1) : 0;
    block_version_free_list = 1;
    block_version_hand      = 1;
    block_version_kept_nr   = 0;
}

static void
block_version_free(uint16_t nr)
{
    block_version_t *v = &block_versions[nr];

    if (v->state == BLOCK_VERSION_KEPT) {
        block_version_kept_nr--;
        if (v->head_mem_block)
            codegen_allocator_free_version(v->head_mem_block);
    }
    v->head_mem_block       = NULL;
    v->state                = BLOCK_VERSION_FREE;
    v->next                 = block_version_free_list;
    block_version_free_list = nr;
}

/*Unlink a kept version from its owner's list and free it*/
static void
block_version_drop_kept(uint16_t nr)
{
    uint16_t *link = &block_version_kept[block_versions[nr].owner];

    while (*link && (*link != nr))
        link = &block_versions[*link].next;
    if (*link)
        *link = block_versions[nr].next;
    block_version_free(nr);
    block_version_owner_sync(block_versions[nr].owner);
}

static uint16_t
block_version_alloc(void)
{
    uint16_t nr;

    if (!block_version_free_list) {
        if (!block_version_kept_nr)
            return 0;
        /*Every record in use: free the next kept version round the pool*/
        for (int c = 0; c < BLOCK_VERSION_NR; c++) {
            nr                 = block_version_hand;
            block_version_hand = (block_version_hand % BLOCK_VERSION_NR) + 1;
            if (block_versions[nr].state == BLOCK_VERSION_KEPT) {
                block_version_drop_kept(nr);
                break;
            }
        }
        if (!block_version_free_list)
            return 0;
    }
    nr                      = block_version_free_list;
    block_version_free_list = block_versions[nr].next;
    block_versions[nr].next = 0;
    return nr;
}

/*The block is being freed: its current version's memory goes with the block,
  its kept versions' memory goes here.*/
static void
block_versions_free_all(uint32_t block_nr)
{
    if (block_version_cur[block_nr]) {
        block_version_free(block_version_cur[block_nr]);
        block_version_cur[block_nr] = 0;
    }
    while (block_version_kept[block_nr]) {
        uint16_t nr                  = block_version_kept[block_nr];
        block_version_kept[block_nr] = block_versions[nr].next;
        block_version_free(nr);
    }
    block_version_owner_sync(block_nr);
}

/*The block's code is being dropped: keep it as a version if it is its current
  one, newest first and no more than BLOCK_VERSION_PER_BLOCK, else free it*/
static void
block_version_keep_cur(codeblock_t *block)
{
    uint32_t block_nr = get_block_nr(block);
    uint16_t cur      = block_version_cur[block_nr];

    if (!block->head_mem_block)
        return;
    if (cur && (block_versions[cur].head_mem_block == block->head_mem_block)) {
        uint16_t nr;
        int      c = 1;

        block_versions[cur].state    = BLOCK_VERSION_KEPT;
        block_version_kept_nr++;
        block_versions[cur].next     = block_version_kept[block_nr];
        block_version_kept[block_nr] = cur;
        for (nr = cur; block_versions[nr].next; nr = block_versions[nr].next) {
            if (++c > BLOCK_VERSION_PER_BLOCK) {
                uint16_t oldest             = block_versions[nr].next;
                block_versions[nr].next     = 0;
                block_versions[oldest].next = 0;
                block_version_free(oldest);
                break;
            }
        }
    } else {
        if (cur)
            block_version_free(cur);
        codegen_allocator_free_version(block->head_mem_block);
    }
    block_version_cur[block_nr] = 0;
    block->head_mem_block       = NULL;
    block_version_owner_sync(block_nr);
}

/*Copy the 64-byte chunk at phys, as fetched, into code and return the
  mapping it came from; NULL if code there is not fetched from host memory.*/
static const uint8_t *
block_version_read_chunk(uint32_t phys, uint8_t *code)
{
    const uint8_t *exec = _mem_exec[phys >> MEM_GRANULARITY_BITS];

    if (exec)
        memcpy(code, &exec[(phys & MEM_GRANULARITY_MASK) & ~0x3f], 64);
    return exec;
}

static int
block_version_chunk_matches(uint32_t phys, const uint8_t *exec, uint64_t mask, const uint8_t *code)
{
    const uint8_t *fetch = _mem_exec[phys >> MEM_GRANULARITY_BITS];

    if (!fetch || (fetch != exec))
        return 0;
    fetch = &fetch[(phys & MEM_GRANULARITY_MASK) & ~0x3f];
    /*Eight bytes at a time: bit n of each mask byte selects byte n*/
    for (int c = 0; c < 64; c += 8) {
        uint64_t m = (mask >> c) & 0xff;
        uint64_t a;
        uint64_t b;

        if (!m)
            continue;
        memcpy(&a, &fetch[c], 8);
        memcpy(&b, &code[c], 8);
        m = (m * 0x0101010101010101ULL) & 0x8040201008040201ULL;
        m = (((m + 0x7f7f7f7f7f7f7f7fULL) & 0x8080808080808080ULL) >> 7) * 0xff;
        if ((a ^ b) & m)
            return 0;
    }
    return 1;
}

#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
/*The words of the version's two chunks that hold bytes it was compiled from,
  as the entry check compares them*/
static void
block_version_chk_fill(block_version_t *v)
{
    v->chk_nr = 0;
    for (int w = 0; w < 16; w++) {
        const uint64_t bits = ((w < 8) ? (v->page_mask >> (w * 8)) : (v->page_mask2 >> ((w - 8) * 8))) & 0xff;
        uint64_t       mask = 0;
        uint64_t       code;

        if (!bits)
            continue;
        for (int b = 0; b < 8; b++) {
            if (bits & (1 << b))
                mask |= (uint64_t) 0xff << (b * 8);
        }
        memcpy(&code, &v->code[w * 8], sizeof(code));
        v->chk_word[v->chk_nr]     = w;
        v->chk[v->chk_nr * 2]      = code & mask;
        v->chk[v->chk_nr * 2 + 1]  = mask;
        v->chk_nr++;
    }
}
#endif

#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
const int codegen_has_block_link = 1;

/*cpu_dyn_block_link as of the last codegen_init()/codegen_reset(). Both free
  every block, so no block runs under a mode other than the one it was
  compiled for: a block compiled without links returns garbage.*/
int codegen_block_link = 1;

/*Last link_gen handed out. Values only grow between wraps, so a block never
  gets back a value a slot may still hold for it.*/
static uint32_t link_gen_last = 0;

#    ifdef CODEGEN_BACKEND_HAS_LINK_SETS
codegen_link_set_t codegen_link_sets[CODEGEN_LINK_SETS] __attribute__((aligned(64)));

/*The orders of e[0..2], most recently used first: 012 021 102 120 201 210*/
const uint8_t codegen_link_set_mtf[6][4] = {
    { 0, 2, 4 },
    { 1, 2, 4 },
    { 0, 2, 5 },
    { 0, 3, 5 },
    { 1, 3, 4 },
    { 1, 3, 5 }
};
/*The least recently used entry of each order*/
static const uint8_t link_set_lru[6] = { 2, 1, 2, 0, 1, 0 };

uint16_t        codegen_link_site_set[BLOCK_SIZE * CODEGEN_LINK_SLOTS];
static uint16_t link_set_free[CODEGEN_LINK_SETS];
static int      link_set_free_nr;

static void
link_sets_reset(void)
{
    memset(codegen_link_site_set, 0, sizeof(codegen_link_site_set));
    link_set_free_nr = 0;
    for (int c = CODEGEN_LINK_SETS - 1; c > 0; c--)
        link_set_free[link_set_free_nr++] = c;
}

static void
link_site_release(int site)
{
    if (codegen_link_site_set[site] && (codegen_link_site_set[site] != CODEGEN_LINK_SET_ONE))
        link_set_free[link_set_free_nr++] = codegen_link_site_set[site];
    codegen_link_site_set[site] = 0;
}

/*The slot is about to be filled for a target at another key: keep the record
  it holds in its set, in the entry that holds that target if one does (as
  the routine would have swapped it), else in the least recently used one.
  With no set yet, the slot gets one the second time its target changes.*/
static void
link_set_keep(codegen_link_slot_t *slot)
{
    const int           site = ((uintptr_t) slot - (uintptr_t) codegen_link_slots) / sizeof(codegen_link_slot_t);
    codegen_link_set_t *set;
    int                 n;

    if (!codegen_link_site_set[site]) {
        codegen_link_site_set[site] = CODEGEN_LINK_SET_ONE;
        return;
    }
    if (codegen_link_site_set[site] == CODEGEN_LINK_SET_ONE) {
        if (!link_set_free_nr)
            return;
        codegen_link_site_set[site] = link_set_free[--link_set_free_nr];
        set                         = &codegen_link_sets[codegen_link_site_set[site]];
        memset(set, 0, sizeof(codegen_link_set_t));
#        if defined(__aarch64__) || defined(_M_ARM64)
        slot->set = set->e[1].set = set->e[2].set = codegen_link_site_set[site];
#        endif
        set->e[0] = *slot;
        return;
    }
    set = &codegen_link_sets[codegen_link_site_set[site]];
    for (n = 0; n < 3; n++) {
        if ((set->e[n].exit_pc == cpu_state.pc) && (set->e[n].exit_cs == cs))
            break;
    }
    if (n == 3)
        n = link_set_lru[set->order];
    set->e[n]  = *slot;
    set->order = codegen_link_set_mtf[set->order][n];
}
#    endif

#    ifdef CODEGEN_BACKEND_HAS_LINK_MCACHE
codegen_link_mcache_t codegen_link_mcache[CODEGEN_LINK_MCACHE_SIZE] __attribute__((aligned(1 << CODEGEN_LINK_MCACHE_SHIFT)));
_Static_assert(sizeof(codegen_link_mcache_t) == (1 << CODEGEN_LINK_MCACHE_SHIFT), "mcache record size");
codeblock_t codegen_link_settled_block = { .used = 1 };
#    endif

void
codegen_link_reset(void)
{
    memset(codegen_link_slots, 0, sizeof(codegen_link_slots));
#    ifdef CODEGEN_BACKEND_HAS_LINK_MCACHE
    memset(codegen_link_mcache, 0, sizeof(codegen_link_mcache));
#    endif
#    ifdef CODEGEN_BACKEND_HAS_LINK_SETS
    link_sets_reset();
#    endif
}

void
codegen_link_block_changed(codeblock_t *block)
{
    if (!codegen_block_link)
        return;

#    ifdef CODEGEN_BACKEND_HAS_LINK_MCACHE
    /*Records here are not checked against link_gen: empty the block's.*/
    if (block->link_mcache) {
        codegen_link_slot_t *rec = &codegen_link_mcache[block->link_mcache - 1].r;

        if (rec->block == block) {
            rec->entry = NULL;
            rec->block = NULL;
        }
        block->link_mcache = 0;
    }
#    endif

    link_gen_last++;
    if (!link_gen_last) {
        /*Wrapped. Empty every slot and renumber every block, so no value
          given out before the wrap can come back to the same block.*/
        codegen_link_reset();
        for (int c = 0; c < BLOCK_SIZE; c++)
            codeblock[c].link_gen = ++link_gen_last;
        link_gen_last++;
    }
    block->link_gen = link_gen_last;
}

/*Empty the slots used by the block's own exits.*/
static void
link_slots_clear(codeblock_t *block)
{
#    ifdef CODEGEN_BACKEND_HAS_LINK_SETS
    for (int c = 0; c < CODEGEN_LINK_SLOTS; c++)
        link_site_release(get_block_nr(block) * CODEGEN_LINK_SLOTS + c);
#    endif
    memset(codegen_link_slots[get_block_nr(block)], 0, sizeof(codegen_link_slots[0]));
}

void
codegen_link_patch(codegen_link_slot_t *from, codeblock_t *block, uint32_t phys)
{
    const uint32_t       virt     = cs + cpu_state.pc;
    uintptr_t           *xlat     = NULL;
    uintptr_t            xlat_val = 0;
    codegen_link_slot_t *slot;

    /*Only a compiled block the hash already points to, and not one that
      checks its code bytes as it is entered (CODEBLOCK_SELF_CHECK).*/
    if ((block->flags & (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_IN_DIRTY_LIST | CODEBLOCK_SELF_CHECK)) != CODEBLOCK_WAS_RECOMPILED)
        return;
    if (codeblock_hash[HASH(phys)] != get_block_nr(block))
        return;
    if (cr0 >> 31) {
        /*The routine can only redo the translation from readlookup2.*/
        xlat = &readlookup2[virt >> 12];
        if ((int) *xlat == -1)
            return;
        xlat_val = *xlat;
    }
    /*phys may have come from get_phys()'s one-entry cache, which can be
      stale. Only fill if a fresh translation gives the same page.*/
    if (cr0 >> 31) {
        if ((((uint32_t) ((xlat_val + (virt & ~0xfff)) - (uintptr_t) ram)) & ~0xfff) != (phys & ~0xfff))
            return;
    } else if (((virt & rammask) & ~0xfff) != (phys & ~0xfff))
        return;

    /*The exit's own slot, whatever it held*/
    slot = from;
#    ifdef CODEGEN_BACKEND_HAS_LINK_SETS
    if (slot->entry && ((slot->exit_pc != cpu_state.pc) || (slot->exit_cs != cs)))
        link_set_keep(slot);
#    endif

    slot->entry      = NULL;
    slot->exit_pc    = cpu_state.pc;
    slot->exit_cs    = cs;
    slot->block      = block;
    slot->gen        = block->link_gen;
    slot->status     = cpu_cur_status;
    slot->npxc       = cpu_state.npxc & 0x0f00;
    slot->cr0_bits   = cr0 & 0xc0000000;
    slot->virt       = virt;
    slot->phys_page  = phys & ~0xfff;
    slot->rammask    = rammask;
    slot->xlat       = xlat;
    slot->xlat_val   = xlat_val;
    slot->mem_exec   = &_mem_exec[phys >> MEM_GRANULARITY_BITS];
    slot->hash       = &codeblock_hash[HASH(phys)];
    slot->block_nr   = get_block_nr(block);
    slot->top_mask   = (block->flags & CODEBLOCK_STATIC_TOP) ? 7 : 0;
    slot->top        = (block->flags & CODEBLOCK_STATIC_TOP) ? block->TOP : 0;
    slot->two_page   = !!block->page_mask2;
    slot->page_mask  = block->page_mask;
    slot->dirty_mask = block->dirty_mask;
    slot->entry      = &block->data[BLOCK_START + CODEGEN_LINK_ENTRY_OFFSET];
#    ifdef CODEGEN_BACKEND_HAS_LINK_MCACHE
    {
        const uint16_t set = codegen_link_site_set[((uintptr_t) slot - (uintptr_t) codegen_link_slots) / sizeof(codegen_link_slot_t)];

        if (set && (set != CODEGEN_LINK_SET_ONE)) {
            const uint32_t idx = CODEGEN_LINK_MCACHE_INDEX(virt);

            codegen_link_mcache[idx].r         = *slot;
            codegen_link_mcache[idx].r.settled = block->used && !block->runs_left && !block->page_mask2;
            block->link_mcache                 = idx + 1;
        }
    }
#    endif
}
#else
const int codegen_has_block_link = 0;
#endif

static void
block_free_list_add(codeblock_t *block)
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
    block->flags    = CODEBLOCK_IN_FREE_LIST;
    block_versions_free_all(get_block_nr(block));
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
    link_slots_clear(block);
#endif
}

static void
block_dirty_list_add(codeblock_t *block)
{
    int l = block_version_kept[get_block_nr(block)] ? DIRTY_LIST_KEPT : DIRTY_LIST_PLAIN;

#ifndef RELEASE_BUILD
    if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
        fatal("block_dirty_list_add: block=%p already in dirty list\n", block);
#endif
    if (block_dirty_list_head[l] != BLOCK_INVALID) {
        codeblock_t *old_head = &codeblock[block_dirty_list_head[l]];

        block->next              = block_dirty_list_head[l];
        block->prev              = BLOCK_INVALID;
        block_dirty_list_head[l] = old_head->prev = get_block_nr(block);
    } else {
        /*List empty*/
        block->prev = block->next = BLOCK_INVALID;
        block_dirty_list_head[l] = block_dirty_list_tail[l] = get_block_nr(block);
    }
    block->flags |= CODEBLOCK_IN_DIRTY_LIST | ((l == DIRTY_LIST_KEPT) ? CODEBLOCK_DIRTY_KEPT : 0);
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#endif
    dirty_list_size[l]++;
    if (dirty_list_size[l] > dirty_list_max[l]) {
        /*Evict oldest block to the free list*/
        codeblock_t *evict_block = &codeblock[block_dirty_list_tail[l]];

#ifndef RELEASE_BUILD
        if (!(evict_block->flags & CODEBLOCK_IN_DIRTY_LIST))
            fatal("block_dirty_list_add: evict_block=%p %x %x not in dirty list\n", evict_block, evict_block->phys, evict_block->flags);
        if (!block_dirty_list_tail[l])
            fatal("block_dirty_list_add - !block_dirty_list_tail\n");
        if (evict_block->prev == BLOCK_INVALID)
            fatal("block_dirty_list_add - evict_block->prev == BLOCK_INVALID\n");
#endif

        block_dirty_list_tail[l]          = evict_block->prev;
        codeblock[evict_block->prev].next = BLOCK_INVALID;

        dirty_list_size[l]--;
        evict_block->flags &= ~(CODEBLOCK_IN_DIRTY_LIST | CODEBLOCK_DIRTY_KEPT | CODEBLOCK_REVIVE_HELD);
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
        /* A block compiled in byte-mask form that can have versions is not
           deleted: it is made checked, without code and off the lists as
           block_checked_invalidate() leaves one, keeping its versions. Its
           next entry (codegen_block_check()) puts back the kept version
           compiled from the bytes there or compiles it with the entry
           check. Its masks are per byte, as the entry tests read them. Not a
           block that reads its opcodes as it runs (CODEBLOCK_NO_OPCODES), nor
           one whose rewrites are scoring towards that (smc_family_score), as
           in invalidate_block(). */
        if (((evict_block->flags & (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_BYTE_MASK)) == (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_BYTE_MASK)) &&
            !evict_block->head_mem_block && ((evict_block->pc & 0xfff) < 0xfc0) &&
            !(evict_block->flags & CODEBLOCK_NO_OPCODES) && !evict_block->smc_family_score) {
            evict_block->next = evict_block->prev = BLOCK_INVALID;
            evict_block->flags |= CODEBLOCK_CHECKED | CODEBLOCK_NO_IMMEDIATES;
            evict_block->dirty_mask = &block_untracked_mask;
            block_chk_dirty_sync(evict_block);
            if (evict_block->dirty_mask2)
                evict_block->dirty_mask2 = &block_untracked_mask;
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
            codegen_link_block_changed(evict_block);
#    endif
            return;
        }
#endif
        delete_dirty_block(evict_block);
    }
}

static void
block_dirty_list_remove(codeblock_t *block)
{
    codeblock_t *prev_block = &codeblock[block->prev];
    codeblock_t *next_block = &codeblock[block->next];
    int          l          = (block->flags & CODEBLOCK_DIRTY_KEPT) ? DIRTY_LIST_KEPT : DIRTY_LIST_PLAIN;

#ifndef RELEASE_BUILD
    if (!(block->flags & CODEBLOCK_IN_DIRTY_LIST))
        fatal("block_dirty_list_remove: block=%p not in dirty list\n", block);
#endif

    /*Is block head of list*/
    if (block->prev == BLOCK_INVALID)
        block_dirty_list_head[l] = block->next;
    else
        prev_block->next = block->next;

    /*Is block tail of list?*/
    if (block->next == BLOCK_INVALID)
        block_dirty_list_tail[l] = block->prev;
    else
        next_block->prev = block->prev;

    dirty_list_size[l]--;
#ifndef RELEASE_BUILD
    if (dirty_list_size[l] < 0)
        fatal("remove - dirty_list_size < 0!\n");
#endif
    block->flags &= ~(CODEBLOCK_IN_DIRTY_LIST | CODEBLOCK_DIRTY_KEPT | CODEBLOCK_REVIVE_HELD);
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#endif
}

int
codegen_purge_purgable_list(void)
{
    if (purgable_page_list_head) {
        page_t *page = &pages[purgable_page_list_head];

        if (page->code_present_mask & page->dirty_mask) {
            codegen_check_flush(page, page->dirty_mask, purgable_page_list_head << 12);

            if (block_free_list)
                return 1;
        }
    }
    return 0;
}

/*The hand of the clock block_recycle_stale() and codegen_delete_random_block()
  sweep the blocks with*/
static int block_clock_hand;

/*One turn of the clock at most: the first block not run since the hand last
  passed it is deleted, to be handed out again. A block run since then is
  passed over once, as codegen_delete_random_block() does. Blocks on the
  dirty list are left to it: they are code rewritten while it ran, the blocks
  most likely to be wanted again, as taking its oldest one showed (a full
  cache of Windows 95 and a game rewriting its code made that block again
  almost every time). Returns 0 after a turn in which every block had run.*/
static int
block_recycle_stale(void)
{
    int block_nr = block_clock_hand & BLOCK_MASK;

    for (int steps = 0; steps < BLOCK_SIZE; steps++, block_nr = (block_nr + 1) & BLOCK_MASK) {
        codeblock_t *block = &codeblock[block_nr];

        if (!block_nr || (block_nr == block_current) || !block->valid || (block->flags & CODEBLOCK_IN_DIRTY_LIST))
            continue;
        if (block->used) {
#ifdef CODEGEN_BACKEND_HAS_LINK_MCACHE
            codegen_link_unsettle(block);
#endif
            block->used = 0;
            continue;
        }
        block_clock_hand = (block_nr + 1) & BLOCK_MASK;
        if (!(block->flags & CODEBLOCK_WAS_RECOMPILED))
            *codegen_ghost_slot(block->phys) = block->phys + 1;
        delete_block(block);
        return 1;
    }
    block_clock_hand = block_nr;
    return 0;
}

/* Pages get on the evict list when a write dirties code on them, but their
   blocks are only checked when one of them is about to run, or when blocks
   run out. Code that never runs again (a program that has exited, its pages
   since reused for data) would keep its blocks, and with them the tracked
   write path for every write to those pages. Check up to max pages of the
   list, so such blocks go and the pages get plain writes again. */
void
codegen_flush_evict_list(int max)
{
    uint32_t nr = purgable_page_list_head;

    while (nr && (max-- > 0)) {
        page_t  *page = &pages[nr];
        uint32_t next = page->evict_next;

        if (page->code_present_mask & page->dirty_mask)
            codegen_check_flush(page, page->dirty_mask, nr << 12);
        nr = next;
    }
}

static codeblock_t *
block_free_list_get(void)
{
    codeblock_t *block = NULL;

    while (!block_free_list) {
        /*Free list is empty: a block not run lately first*/
        if (block_recycle_stale())
            continue;
        /*Then the dirty lists, the plain one first*/
        int l = block_dirty_list_tail[DIRTY_LIST_PLAIN] ? DIRTY_LIST_PLAIN : DIRTY_LIST_KEPT;

        if (block_dirty_list_tail[l]) {
#ifndef RELEASE_BUILD
            if (dirty_list_size[l] <= 0)
                fatal("get - dirty_list_size <= 0!\n");
#endif
            /*Reuse oldest block*/
            block = &codeblock[block_dirty_list_tail[l]];

            block_dirty_list_tail[l] = block->prev;
            if (block->prev == BLOCK_INVALID)
                block_dirty_list_head[l] = BLOCK_INVALID;
            else
                codeblock[block->prev].next = BLOCK_INVALID;
            dirty_list_size[l]--;
            block->flags &= ~(CODEBLOCK_IN_DIRTY_LIST | CODEBLOCK_DIRTY_KEPT | CODEBLOCK_REVIVE_HELD);
            delete_dirty_block(block);
            block_free_list = get_block_nr(block);
            break;
        }
        /*Free list is empty - free up a block*/
        if (!codegen_purge_purgable_list())
            codegen_delete_random_block(0);
    }

    block           = &codeblock[block_free_list];
    block_free_list = block->next;
    block->flags &= ~CODEBLOCK_IN_FREE_LIST;
    block->next = 0;
    return block;
}

int         family_review_count;
static void family_review_reset(void);

void
codegen_init(void)
{
    codegen_check_regs();
    codegen_allocator_init();
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_block_link = cpu_dyn_block_link;
#endif

    codegen_backend_init();
    block_versions_init();
    family_review_reset();
    block_free_list = 0;
    for (uint32_t c = 0; c < BLOCK_SIZE; c++)
        block_free_list_add(&codeblock[c]);
    for (int l = 0; l < DIRTY_LISTS; l++) {
        block_dirty_list_head[l] = block_dirty_list_tail[l] = 0;
        dirty_list_size[l]                                  = 0;
    }
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_reset();
#endif
#ifdef DEBUG_EXTRA
    memset(instr_counts, 0, sizeof(instr_counts));
#endif
}

void
codegen_reset(void)
{
    int c;

    /*The page table may already have been replaced. Drop its entry numbers
      before unlinking the old blocks, then reset the chunk table at once.*/
    if (block_chunk_page)
        memset(block_chunk_page, 0, block_chunk_page_nr * sizeof(uint32_t));
    for (c = 1; c < BLOCK_SIZE; c++) {
        codeblock_t *block = &codeblock[c];

        if (block->valid) {
            block->phys   = 0;
            block->phys_2 = 0;
            /* Not taken off the page lists: phys no longer names the page,
               and every page's lists are emptied below. */
            block->flags &= ~(CODEBLOCK_IN_PAGE_LIST | CODEBLOCK_HAS_PAGE2);
            delete_block(block);
        }
    }

    memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
    memset(codeblock_hash, 0, HASH_SIZE * sizeof(uint32_t));
    codegen_evict_thrash = 0;
    codegen_reset_pending = 0;
    family_review_reset();
    mem_reset_page_blocks();
    block_chunk_lists_nr   = 0;
    block_chunk_lists_free = 0;
    block_list_stamp       = 0;

    block_free_list = 0;
    for (c = 0; c < BLOCK_SIZE; c++) {
        codeblock[c].valid = 0;
        block_free_list_add(&codeblock[c]);
    }
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_reset();
    codegen_block_link = cpu_dyn_block_link;
#endif
}

void
dump_block(void)
{
#if 0
    codeblock_t *block = pages[0x119000 >> 12].block;

    pclog("dump_block:\n");
    while (block) {
        uint32_t start_pc = (block->pc & 0xffc) | (block->phys & ~0xfff);
        uint32_t end_pc = (block->endpc & 0xffc) | (block->phys & ~0xfff);

        pclog(" %p : %08x-%08x  %08x-%08x %p %p\n", (void *)block, start_pc, end_pc,  block->pc, block->endpc, (void *)block->prev, (void *)block->next);

        if (!block->pc)
            fatal("Dead PC=0\n");

        block = block->next;
    }

    pclog("dump_block done\n");*/
#endif
}

/*The page's entry in the chunk list table, made if it has none*/
static uint32_t
block_chunk_lists_get(uint32_t page_nr)
{
    if (page_nr >= block_chunk_page_nr) {
        uint32_t nr = (page_nr + 1 > 2 * block_chunk_page_nr) ? (page_nr + 1) : (2 * block_chunk_page_nr);

        block_chunk_page = realloc(block_chunk_page, nr * sizeof(uint32_t));
        if (!block_chunk_page)
            fatal("block_chunk_lists_get: out of memory\n");
        memset(&block_chunk_page[block_chunk_page_nr], 0, (nr - block_chunk_page_nr) * sizeof(uint32_t));
        block_chunk_page_nr = nr;
    }
    if (!block_chunk_page[page_nr]) {
        uint32_t entry = block_chunk_lists_free;

        if (entry)
            block_chunk_lists_free = block_chunk_lists[entry].next_free;
        else {
            if (!block_chunk_lists_nr)
                block_chunk_lists_nr = 1;
            if (block_chunk_lists_nr >= block_chunk_lists_max) {
                block_chunk_lists_max = block_chunk_lists_max ? (2 * block_chunk_lists_max) : 256;
                block_chunk_lists     = realloc(block_chunk_lists, block_chunk_lists_max * sizeof(block_chunk_lists_t));
                if (!block_chunk_lists)
                    fatal("block_chunk_lists_get: out of memory\n");
            }
            entry = block_chunk_lists_nr++;
            memset(&block_chunk_lists[entry], 0, sizeof(block_chunk_lists_t));
        }
        block_chunk_lists[entry].mask_base = pages[page_nr].byte_dirty_mask;
        block_chunk_page[page_nr]          = entry;
    }
    return block_chunk_page[page_nr];
}

static void
block_chunk_lists_release(uint32_t page_nr)
{
    const uint32_t entry = block_chunk_page[page_nr];

    if (entry && !pages[page_nr].block && !pages[page_nr].block_2) {
        block_chunk_page[page_nr]          = 0;
        block_chunk_lists[entry].next_free = block_chunk_lists_free;
        block_chunk_lists_free             = entry;
    }
}

/*The chunk list a block's dirty test reads, for its first (dirty_mask) or
  second (dirty_mask2) page list: the chunk of the byte dirty word of
  mask_base it points at, or that of a checked block's untracked one (whose
  test reads the page's array, mask_base while the chunk lists are walked),
  else CHUNK_LIST_OTHER*/
static uint8_t
block_chunk_key(uint32_t phys, const uint64_t *dirty_mask, const uint64_t *mask_base)
{
    const int chunk = (phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

    if (mask_base && (pages[phys >> 12].byte_dirty_mask == mask_base) &&
        ((dirty_mask == &block_untracked_mask) || (dirty_mask == &mask_base[chunk])))
        return chunk;
    return CHUNK_LIST_OTHER;
}

static int
block_list_stamp_cmp(const void *a, const void *b)
{
    const uint64_t sa = codeblock[*(const uint32_t *) a].list_stamp;
    const uint64_t sb = codeblock[*(const uint32_t *) b].list_stamp;

    return (sa > sb) - (sa < sb);
}

/*The list stamps of every listed block numbered again from 1, in the order
  they have: every listed block is on one chunk list of its first page*/
static void
block_list_stamp_renumber(void)
{
    static uint32_t order[BLOCK_SIZE];
    uint32_t        nr = 0;

    for (uint32_t e = 1; e < block_chunk_lists_nr; e++) {
        for (int k = 0; k <= CHUNK_LIST_OTHER; k++) {
            for (uint32_t b = block_chunk_lists[e].head[0][k]; b; b = codeblock[b].chunk_next)
                order[nr++] = b;
        }
    }
    qsort(order, nr, sizeof(order[0]), block_list_stamp_cmp);
    for (uint32_t c = 0; c < nr; c++)
        codeblock[order[c]].list_stamp = c + 1;
    block_list_stamp = nr;
}

static void
add_to_chunk_lists(codeblock_t *block)
{
    uint32_t             block_nr = get_block_nr(block);
    block_chunk_lists_t *lists;

    /*The merge in codegen_check_flush() needs the page lists' order*/
    if (block_list_stamp == UINT64_MAX)
        block_list_stamp_renumber();
    block->list_stamp  = ++block_list_stamp;
    block->chunk_lists = block_chunk_lists_get(block->phys >> 12);
    block->chunk_key   = block_chunk_key(block->phys, block->dirty_mask, block_chunk_lists[block->chunk_lists].mask_base);
    lists              = &block_chunk_lists[block->chunk_lists];
    block->chunk_prev  = BLOCK_INVALID;
    block->chunk_next  = lists->head[0][block->chunk_key];
    if (block->chunk_next)
        codeblock[block->chunk_next].chunk_prev = block_nr;
    lists->head[0][block->chunk_key] = block_nr;

    if (block->page_mask2) {
        block->chunk_lists_2 = block_chunk_lists_get(block->phys_2 >> 12);
        block->chunk_key_2   = block_chunk_key(block->phys_2, block->dirty_mask2, block_chunk_lists[block->chunk_lists_2].mask_base);
        lists                = &block_chunk_lists[block->chunk_lists_2];
        block->chunk_prev_2  = BLOCK_INVALID;
        block->chunk_next_2  = lists->head[1][block->chunk_key_2];
        if (block->chunk_next_2)
            codeblock[block->chunk_next_2].chunk_prev_2 = block_nr;
        lists->head[1][block->chunk_key_2] = block_nr;
    }
}

static void
remove_from_chunk_list(codeblock_t *block)
{
    if (block->chunk_prev)
        codeblock[block->chunk_prev].chunk_next = block->chunk_next;
    else
        block_chunk_lists[block->chunk_lists].head[0][block->chunk_key] = block->chunk_next;
    if (block->chunk_next)
        codeblock[block->chunk_next].chunk_prev = block->chunk_prev;
}

static void
remove_from_chunk_list_2(codeblock_t *block)
{
    if (block->chunk_prev_2)
        codeblock[block->chunk_prev_2].chunk_next_2 = block->chunk_next_2;
    else
        block_chunk_lists[block->chunk_lists_2].head[1][block->chunk_key_2] = block->chunk_next_2;
    if (block->chunk_next_2)
        codeblock[block->chunk_next_2].chunk_prev_2 = block->chunk_prev_2;
}

static void
add_to_block_list(codeblock_t *block)
{
    uint32_t block_prev_nr = pages[block->phys >> 12].block;
    uint32_t block_nr      = get_block_nr(block);

#ifndef RELEASE_BUILD
    if (!block->page_mask)
        fatal("add_to_block_list - mask = 0 %" PRIx64 " %" PRIx64 "\n", block->page_mask, block->page_mask2);
#endif

    if (block_prev_nr) {
        block->next                    = block_prev_nr;
        codeblock[block_prev_nr].prev  = block_nr;
        pages[block->phys >> 12].block = block_nr;
    } else {
        block->next                    = BLOCK_INVALID;
        pages[block->phys >> 12].block = block_nr;
    }
    block->flags |= CODEBLOCK_IN_PAGE_LIST;

    if (block->next) {
#ifndef RELEASE_BUILD
        if (!codeblock[block->next].valid)
            fatal("!block->next->valid %p %p %x %x\n", (void *) &codeblock[block->next], (void *) codeblock, block_current, block_pos);
#endif
    }

    if (block->page_mask2) {
        block->flags |= CODEBLOCK_HAS_PAGE2;

        block_prev_nr = pages[block->phys_2 >> 12].block_2;

        if (block_prev_nr) {
            block->next_2                      = block_prev_nr;
            codeblock[block_prev_nr].prev_2    = block_nr;
            pages[block->phys_2 >> 12].block_2 = block_nr;
        } else {
            block->next_2                      = BLOCK_INVALID;
            pages[block->phys_2 >> 12].block_2 = block_nr;
        }
    }
    add_to_chunk_lists(block);
}

static void
remove_from_block_list(codeblock_t *block, UNUSED(uint32_t pc))
{
    /* Not page_mask: a recompile clears that while the block stays listed,
       and a block given up before its first instruction is marked would be
       freed still on the list, which then loops when it is listed again */
    if (!(block->flags & CODEBLOCK_IN_PAGE_LIST))
        return;
    block->flags &= ~CODEBLOCK_IN_PAGE_LIST;
#ifndef RELEASE_BUILD
    if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
        fatal("remove_from_block_list: in dirty list\n");
#endif
    remove_from_chunk_list(block);
    if (block->prev) {
        codeblock[block->prev].next = block->next;
        if (block->next)
            codeblock[block->next].prev = block->prev;
    } else {
        pages[block->phys >> 12].block = block->next;
        if (block->next)
            codeblock[block->next].prev = BLOCK_INVALID;
        else {
            mem_write_page_uncoded(block->phys);
            block_chunk_lists_release(block->phys >> 12);
        }
    }

    if (!(block->flags & CODEBLOCK_HAS_PAGE2)) {
#ifndef RELEASE_BUILD
        if (block->prev_2 || block->next_2)
            fatal("Invalid block_2 %x %p %08x\n", block->flags, block, block->phys);
#endif
        return;
    }
    block->flags &= ~CODEBLOCK_HAS_PAGE2;
    remove_from_chunk_list_2(block);

    if (block->prev_2) {
        codeblock[block->prev_2].next_2 = block->next_2;
        if (block->next_2)
            codeblock[block->next_2].prev_2 = block->prev_2;
    } else {
        pages[block->phys_2 >> 12].block_2 = block->next_2;
        if (block->next_2)
            codeblock[block->next_2].prev_2 = BLOCK_INVALID;
        else {
            mem_write_page_uncoded(block->phys_2);
            block_chunk_lists_release(block->phys_2 >> 12);
        }
    }
}

/*Whether a block that reads its opcodes as it runs has opcodes that differ
  now from when it compiled.*/
static int
block_family_ops_changed(const codeblock_t *block)
{
    uint16_t               cur;
    const block_version_t *v;

    if (!(block->flags & CODEBLOCK_OPCODE_FAMILY))
        return 0;
    cur = block_version_cur[get_block_nr(block)];
    if (!cur)
        return 0;
    v = &block_versions[cur];
    if (v->head_mem_block != block->head_mem_block)
        return 0;
    if (v->family_ops[0] && !block_version_chunk_matches(block->phys, v->exec, v->family_ops[0], v->code))
        return 1;
    if (v->family_ops[1] && v->exec_2 && !block_version_chunk_matches(block->phys + 0x40, v->exec_2, v->family_ops[1], &v->code[64]))
        return 1;
    return 0;
}

/*Bytes of the chunk at phys, under mask, that differ from code. -1 if the
  chunk is no longer mapped where it was.*/
static uint64_t
block_version_chunk_diff_mask(uint32_t phys, const uint8_t *exec, uint64_t mask, const uint8_t *code)
{
    const uint8_t *fetch = _mem_exec[phys >> MEM_GRANULARITY_BITS];
    uint64_t       diff  = 0;

    if (!fetch || (fetch != exec))
        return (uint64_t) -1;
    fetch = &fetch[(phys & MEM_GRANULARITY_MASK) & ~0x3f];
    while (mask) {
        int c = __builtin_ctzll(mask);

        if (fetch[c] != code[c])
            diff |= (uint64_t) 1 << c;
        mask &= mask - 1;
    }
    return diff;
}

/*A block compiled for its bytes with CODEBLOCK_NO_IMMEDIATES is being
  rewritten: whether only opcodes it would read as it runs with
  CODEBLOCK_NO_OPCODES changed, each to another of that family. Such code
  would have run on through the rewrite.*/
static int
block_family_cand_changed(const codeblock_t *block)
{
    uint16_t               cur;
    const block_version_t *v;
    const uint8_t         *fetch;
    uint64_t               diff[2];

    if ((block->flags & (CODEBLOCK_NO_IMMEDIATES | CODEBLOCK_OPCODE_FAMILY)) != CODEBLOCK_NO_IMMEDIATES)
        return 0;
    cur = block_version_cur[get_block_nr(block)];
    if (!cur)
        return 0;
    v = &block_versions[cur];
    if ((v->head_mem_block != block->head_mem_block) || (v->flags & CODEBLOCK_OPCODE_FAMILY) || !(v->family_ops[0] | v->family_ops[1]))
        return 0;
    diff[0] = block_version_chunk_diff_mask(block->phys, v->exec, v->page_mask, v->code);
    diff[1] = v->page_mask2 ? block_version_chunk_diff_mask(v->phys_2, v->exec_2, v->page_mask2, &v->code[64]) : 0;
    if (!(diff[0] | diff[1]) || (diff[0] & ~v->family_ops[0]) || (diff[1] & ~v->family_ops[1]))
        return 0;
    for (int h = 0; h < 2; h++) {
        if (!diff[h])
            continue;
        fetch = &_mem_exec[(h ? v->phys_2 : block->phys) >> MEM_GRANULARITY_BITS][((h ? v->phys_2 : block->phys) & MEM_GRANULARITY_MASK) & ~0x3f];
        for (uint64_t m = diff[h]; m; m &= m - 1) {
            if ((fetch[__builtin_ctzll(m)] & 0xc7) != 0x05)
                return 0;
        }
    }
    return 1;
}

/*Blocks that read their opcodes as they run. Only a rewrite decides whether a
  block keeps doing so, and opcode writes no longer rewrite it: code whose
  opcodes stopped changing would stay in that slower form for good. Checked
  after each CPU run (codegen_family_review()).*/
#define FAMILY_REVIEW_MAX    256
#define FAMILY_REVIEW_STABLE 2

/*A block also comes off when its opcodes change, but less often than every
  SMC_FAMILY_CAND_MIN_RUNS runs on average over FAMILY_REVIEW_CHANGES
  changes: compiling it for its bytes after each change costs less*/
#define FAMILY_REVIEW_CHANGES 16

static uint32_t family_review_nr[FAMILY_REVIEW_MAX];
static uint8_t  family_review_stable[FAMILY_REVIEW_MAX];
static uint64_t family_review_listed[BLOCK_SIZE / 64];
/*Per entry: the block's opcode bytes as last seen, block_family_runs then,
  and the runs and opcode changes counted since the last decision*/
static uint8_t  family_review_code[FAMILY_REVIEW_MAX][128];
static uint32_t family_review_runs[FAMILY_REVIEW_MAX];
static uint32_t family_review_acc_runs[FAMILY_REVIEW_MAX];
static uint32_t family_review_acc_changes[FAMILY_REVIEW_MAX];

static void
family_review_start(int c, uint32_t block_nr)
{
    family_review_stable[c] = 0;
    memcpy(family_review_code[c], block_versions[block_version_cur[block_nr]].code, 128);
    family_review_runs[c]        = block_family_runs[block_nr];
    family_review_acc_runs[c]    = 0;
    family_review_acc_changes[c] = 0;
}

/*Opcodes of the version v of a listed block changed since last seen*/
static int
family_review_changes(int c, uint32_t phys, const block_version_t *v)
{
    int n = 0;

    for (int h = 0; h < 2; h++) {
        uint64_t       m     = v->family_ops[h];
        uint32_t       p     = h ? (phys + 0x40) : phys;
        const uint8_t *fetch = _mem_exec[p >> MEM_GRANULARITY_BITS];

        if (!m || !fetch || (fetch != (h ? v->exec_2 : v->exec)))
            continue;
        fetch = &fetch[(p & MEM_GRANULARITY_MASK) & ~0x3f];
        for (; m; m &= m - 1) {
            int b = __builtin_ctzll(m);

            if (fetch[b] != family_review_code[c][h * 64 + b]) {
                family_review_code[c][h * 64 + b] = fetch[b];
                n++;
            }
        }
    }
    return n;
}

static void
family_review_add(uint32_t block_nr)
{
    uint64_t bit = 1ULL << (block_nr & 63);

    if (family_review_listed[block_nr >> 6] & bit) {
        for (int c = 0; c < family_review_count; c++) {
            if (family_review_nr[c] == block_nr) {
                family_review_start(c, block_nr);
                break;
            }
        }
        return;
    }
    if (family_review_count >= FAMILY_REVIEW_MAX)
        return;
    family_review_listed[block_nr >> 6] |= bit;
    family_review_nr[family_review_count] = block_nr;
    family_review_start(family_review_count, block_nr);
    family_review_count++;
}

static void
family_review_reset(void)
{
    memset(family_review_listed, 0, sizeof(family_review_listed));
    family_review_count = 0;
}

void
codegen_family_review(void)
{
    int c = 0;

    while (c < family_review_count) {
        uint32_t               block_nr = family_review_nr[c];
        codeblock_t           *block    = &codeblock[block_nr];
        uint16_t               cur      = block_version_cur[block_nr];
        const block_version_t *v        = &block_versions[cur];
        int                    drop     = 0;
        int                    changes  = 0;

        if (!block->valid || !cur || (v->head_mem_block != block->head_mem_block) ||
            ((block->flags & (CODEBLOCK_OPCODE_FAMILY | CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_IN_DIRTY_LIST)) != (CODEBLOCK_OPCODE_FAMILY | CODEBLOCK_WAS_RECOMPILED)))
            drop = 1;
        else {
            uint32_t runs = block_family_runs[block_nr];

            changes = family_review_changes(c, block->phys, v);
            family_review_acc_runs[c] += (runs >= family_review_runs[c]) ? (runs - family_review_runs[c]) : runs;
            family_review_runs[c] = runs;
            family_review_acc_changes[c] += changes;
        }
        if (drop)
            ;
        else if (family_review_acc_changes[c] >= FAMILY_REVIEW_CHANGES) {
            if (family_review_acc_runs[c] >= (uint32_t) SMC_FAMILY_CAND_MIN_RUNS * family_review_acc_changes[c]) {
                /* Its opcodes change, but too seldom: compile it for
                   them, each time. */
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
                codegen_link_block_changed(block);
#endif
                block->flags &= ~(CODEBLOCK_NO_OPCODES | CODEBLOCK_OPCODE_FAMILY | CODEBLOCK_WAS_RECOMPILED);
                block->smc_family_score       = 0;
                block_family_hold[block_nr]   = 0;
                drop                          = 1;
            } else {
                family_review_stable[c]      = 0;
                family_review_acc_runs[c]    = 0;
                family_review_acc_changes[c] = 0;
            }
        } else if (changes || block_family_ops_changed(block))
            family_review_stable[c] = 0;
        else if (codeblock_ran(block->runs_left, SMC_FAMILY_MIN_RUNS) && (++family_review_stable[c] >= FAMILY_REVIEW_STABLE)) {
            /* Still the opcodes it compiled with, over a whole check
               interval: compile it for them. One more short-run rewrite
               makes it a family block again (smc_family_ready()). */
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
            codegen_link_block_changed(block);
#endif
            block->flags &= ~(CODEBLOCK_NO_OPCODES | CODEBLOCK_OPCODE_FAMILY | CODEBLOCK_WAS_RECOMPILED);
            if (block->smc_family_score >= SMC_FAMILY_SCORE)
                block->smc_family_score = SMC_FAMILY_SCORE - 1;
            drop = 1;
        }

        if (drop) {
            family_review_listed[block_nr >> 6] &= ~(1ULL << (block_nr & 63));
            family_review_count--;
            family_review_nr[c]          = family_review_nr[family_review_count];
            family_review_stable[c]      = family_review_stable[family_review_count];
            family_review_runs[c]        = family_review_runs[family_review_count];
            family_review_acc_runs[c]    = family_review_acc_runs[family_review_count];
            family_review_acc_changes[c] = family_review_acc_changes[family_review_count];
            memcpy(family_review_code[c], family_review_code[family_review_count], 128);
        } else
            c++;
    }
}

static void
invalidate_block(codeblock_t *block)
{
    uint32_t old_pc = block->pc;

#ifndef RELEASE_BUILD
    if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
        fatal("invalidate_block: already in dirty list\n");
    if (!block->valid)
        fatal("Invalidating deleted block\n");
#endif
    /* A block that compiled, then got rewritten before it ran often enough
       from block->data to make up for compiling it, cost more than
       interpreting it would have: count it as a strike against the block.
       One that ran often enough paid for itself, so it clears the block's
       strikes instead. One that was only marked, never compiled, cost
       nothing extra and tells neither. */
    /* The page keeps a count too: code generated afresh at new addresses
       each time never revisits a block, so only the page can tell. A
       block that paid for itself only takes one strike off the page, so
       one lucky block does not wipe the history of a page whose code
       keeps being replaced. A block struck again backs off at least as
       long as the page says, up to the cap it would have on its own. */
    if (block->flags & CODEBLOCK_WAS_RECOMPILED) {
        page_t *page = &pages[block->phys >> 12];

        page->code_rewritten = codegen_page_stamp(tsc);
        smc_thrash_rewritten(&block->smc_strikes, &page->smc_strikes,
                             codeblock_ran(block->runs_left, SMC_THRASH_MIN_RUNS),
                             tsc, &block->smc_cooldown_until);
        {
            uint8_t *hold     = &block_family_hold[get_block_nr(block)];
            int      ran_long = codeblock_ran(block->runs_left, SMC_FAMILY_MIN_RUNS);
            int      keep     = 0;

            /* Code whose opcodes change all over gives one block of it a
               change only now and then: often just over
               SMC_FAMILY_MIN_RUNS runs apart, where reading them as it
               runs still pays. Such a block counts its runs further
               (block_family_runs). */
            if (ran_long && block_family_cand_changed(block))
                ran_long = block_family_runs[get_block_nr(block)] >= SMC_FAMILY_CAND_MIN_RUNS;
            block_family_runs[get_block_nr(block)] = 0;

            if (!ran_long || !(block->flags & CODEBLOCK_OPCODE_FAMILY))
                smc_family_hold_rewritten(hold, ran_long);
            else if (block_family_ops_changed(block))
                keep = smc_family_hold(hold);
            else
                *hold = 0;

            if (!keep)
                smc_family_rewritten(&block->smc_family_score, ran_long);
        }
    }
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    /* Rewritten again after it was compiled to read its immediates from
       memory: its next compiles get the entry check, and while its current
       code has one, writes stop marking the block and the check compares its
       own bytes with memory on every entry instead (codegen_block_check()).
       Only a block with versions, whose two chunks are in one page, and not
       one that reads its opcodes as it runs (CODEBLOCK_NO_OPCODES). Nor one
       whose rewrites are scoring towards that (smc_family_score): a checked
       block stays off the dirty list, where the loop moves a block on to
       reading its opcodes, so it would keep recompiling for every change. */
    if (((block->flags & (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_BYTE_MASK | CODEBLOCK_NO_IMMEDIATES | CODEBLOCK_NO_OPCODES)) ==
         (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_BYTE_MASK | CODEBLOCK_NO_IMMEDIATES)) &&
        ((block->pc & 0xfff) < 0xfc0) && !block->smc_family_score)
        block->flags |= CODEBLOCK_CHECKED;
#endif
    remove_from_block_list(block, old_pc);
    block_version_keep_cur(block);
    block->head_mem_block = NULL;
    /*After the version is kept: that picks the list*/
    block_dirty_list_add(block);
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    link_slots_clear(block);
#endif
}

static void
delete_block(codeblock_t *block)
{
    uint32_t old_pc = block->pc;

    if (block == &codeblock[codeblock_hash[HASH(block->phys)]])
        codeblock_hash[HASH(block->phys)] = BLOCK_INVALID;

#ifndef RELEASE_BUILD
    if (!block->valid)
        fatal("Deleting deleted block\n");
#endif
    block->valid = 0;

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

static void
delete_dirty_block(codeblock_t *block)
{
    if (block == &codeblock[codeblock_hash[HASH(block->phys)]])
        codeblock_hash[HASH(block->phys)] = BLOCK_INVALID;

#ifndef RELEASE_BUILD
    if (!block->valid)
        fatal("Deleting deleted block\n");
#endif
    block->valid = 0;

    codeblock_tree_delete(block);
    block_free_list_add(block);
}

void
codegen_delete_block(codeblock_t *block)
{
    if (block->valid)
        delete_block(block);
}

/*The page is moving to another array. Delete its blocks and their kept
  versions, whose entry checks can have the old array built into them.*/
void
codegen_invalidate_page_masks(page_t *page)
{
    const uint32_t page_nr   = (uint32_t) (page - pages);
    const uint32_t recording = ((recomp_page != -1) && codeblock[block_current].valid) ? block_current : 0;

    if (recording) {
        codeblock_t *block = &codeblock[recording];

        /*The instruction being recorded can change its own mapping. The
          loop discards the unfinished record when the instruction returns.*/
        codegen_record_faulted = 1;
        if (((block->phys >> 12) == page_nr) ||
            ((block->flags & CODEBLOCK_HAS_PAGE2) && ((block->phys_2 >> 12) == page_nr))) {
            remove_from_block_list(block, block->pc);
            block_versions_free_all(recording);
        }
    }

    /*The tree also has blocks off the page lists that still own versions.*/
    while (page->head) {
        uint32_t block_nr = page->head;

        if (block_nr == recording) {
            codeblock_t *block = &codeblock[block_nr];

            block_nr = block->left ? block->left : block->right;
            if (!block_nr)
                break;
        }
        delete_block(&codeblock[block_nr]);
    }
    while (page->block_2)
        delete_block(&codeblock[page->block_2]);

    /*The queued dirty bits belong to the old array, which may be shared.
      Page number 0 is the end of the evict list.*/
    if (page_nr && page_in_evict_list(page))
        page_remove_from_evict_list(page);
}

int      codegen_evict_thrash = 0;
int      codegen_reset_pending = 0;
uint32_t codegen_admit_filter[1 << (32 - CODEGEN_ADMIT_SHIFT)];
uint32_t codegen_evict_ghost[1 << (32 - CODEGEN_GHOST_SHIFT)];

void
codegen_delete_random_block(int required_mem_block)
{
    int block_nr = block_clock_hand & BLOCK_MASK;
    int steps    = 0;

    /* A second chance, as a clock: a block run since the sweep last came by
       is passed over once; one full turn clears them all, so the sweep
       ends. Evicting blindly threw out code about to run again. */
    while (1) {
        if (block_nr && block_nr != block_current) {
            codeblock_t *block = &codeblock[block_nr];

            if (block->valid && block->used) {
#ifdef CODEGEN_BACKEND_HAS_LINK_MCACHE
                codegen_link_unsettle(block);
#endif
                block->used = 0;
                block_nr    = (block_nr + 1) & BLOCK_MASK;
                continue;
            }
            if (block->valid && (!required_mem_block || block->head_mem_block)) {
                block_clock_hand = (block_nr + 1) & BLOCK_MASK;
                if (!(block->flags & CODEBLOCK_WAS_RECOMPILED))
                    *codegen_ghost_slot(block->phys) = block->phys + 1;
                delete_block(block);
                return;
            }
        }
        block_nr = (block_nr + 1) & BLOCK_MASK;
        /* Two turns take every second chance: a third finding nothing is
           a cache with no block it may give up. */
        if (++steps > (3 * BLOCK_SIZE))
            fatal("codegen_delete_random_block: no block can be evicted\n");
    }
}

/* A block thrown out because its code was written: the lines written are in
   the processor's caches now, as written data, so the code fetched from them
   next is not a miss (cpu_code_line_tags). mask has the block's written
   64-byte chunks on its first page, or with second set on its second. */
static void
codegen_code_lines_written(const codeblock_t *block, uint64_t mask, int second)
{
    uint32_t line = (block->pc >> CPU_CODE_LINE_SHIFT) + second;

    if (!cpu_code_miss_cycles)
        return;

    if (!(block->flags & CODEBLOCK_BYTE_MASK)) {
        line = ((block->pc & ~0xfff) >> CPU_CODE_LINE_SHIFT) + (second ? 64 : 0);
        while (mask) {
            const uint32_t tag = line + __builtin_ctzll(mask) + 1;

            cpu_code_line_tags[tag & (CPU_CODE_LINES - 1)] = tag;
            mask &= mask - 1;
        }
        return;
    }
    cpu_code_line_tags[(line + 1) & (CPU_CODE_LINES - 1)] = line + 1;
}

/*A block on the page's first list, as the flush tests and invalidates it*/
static void
check_flush_block(codeblock_t *block)
{
    if (*block->dirty_mask & block->page_mask) {
        codegen_code_lines_written(block, *block->dirty_mask & block->page_mask, 0);
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
        if (block_checked_has_code(block))
            block_checked_invalidate(block);
        else
#endif
            invalidate_block(block);
    }
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    else if ((block->dirty_mask == &block_untracked_mask) && block->head_mem_block && block_checked_written(block))
        block_checked_invalidate(block);
#endif
}

/*A block on the page's second list (page_mask2 in this page)*/
static void
check_flush_block_2(codeblock_t *block)
{
    if (*block->dirty_mask2 & block->page_mask2) {
        codegen_code_lines_written(block, *block->dirty_mask2 & block->page_mask2, 1);
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
        if (block_checked_has_code(block))
            block_checked_invalidate(block);
        else
#endif
            invalidate_block(block);
    }
}

/*Most chunk lists one walk merges; past that the whole page list is walked*/
#define CHECK_FLUSH_MAX_LISTS 8

/*The blocks of one page list that are on the given chunk lists, in the page
  list's order (falling list_stamp), tested as the walk of the whole list
  tests them. A block on any other chunk list reads only dirty bits that are
  clear, so that walk would pass it over.*/
static void
check_flush_chunk_lists(const uint32_t *heads, uint64_t chunks, int second)
{
    uint32_t cur[CHECK_FLUSH_MAX_LISTS + 1];
    int      nr = 0;

    if (heads[CHUNK_LIST_OTHER])
        cur[nr++] = heads[CHUNK_LIST_OTHER];
    for (; chunks; chunks &= chunks - 1) {
        const uint32_t head = heads[__builtin_ctzll(chunks)];

        if (head)
            cur[nr++] = head;
    }
    while (nr) {
        codeblock_t *block;
        uint32_t     next;
        int          newest = 0;

        for (int c = 1; c < nr; c++) {
            if (codeblock[cur[c]].list_stamp > codeblock[cur[newest]].list_stamp)
                newest = c;
        }
        block = &codeblock[cur[newest]];
        next  = second ? block->chunk_next_2 : block->chunk_next;
        if (next)
            cur[newest] = next;
        else
            cur[newest] = cur[--nr];
        if (second)
            check_flush_block_2(block);
        else
            check_flush_block(block);
    }
}

void
codegen_check_flush(page_t *page, UNUSED(uint64_t mask), UNUSED(uint32_t phys_addr))
{
    const uint32_t page_nr                = (uint32_t) (page - pages);
    const uint32_t lists_nr               = (page_nr < block_chunk_page_nr) ? block_chunk_page[page_nr] : 0;
    uint32_t       block_nr               = page->block;
    uint64_t       chunks                 = 0;
    int            remove_from_evict_list = 0;
    int            whole                  = !lists_nr || (page->mem == page_ff) || (page->byte_dirty_mask != block_chunk_lists[lists_nr].mask_base);

    /*The chunks with dirty bytes*/
    for (int c = 0; !whole && (c < 64); c++) {
        if (page->byte_dirty_mask[c])
            chunks |= (uint64_t) 1 << c;
    }
    /*A checked block's first-list test reads its second chunk's bits too*/
    if (!whole && (__builtin_popcountll(chunks | (chunks >> 1)) < CHECK_FLUSH_MAX_LISTS))
        check_flush_chunk_lists(block_chunk_lists[lists_nr].head[0], chunks | (chunks >> 1), 0);
    else {
        while (block_nr) {
            codeblock_t *block      = &codeblock[block_nr];
            uint32_t     next_block = block->next;

            check_flush_block(block);
#ifndef RELEASE_BUILD
            if (block_nr == next_block)
                fatal("Broken 1\n");
#endif
            block_nr = next_block;
        }
    }

    /*The first walk can release the entry if the second list is empty.*/
    if (page->block_2 && !whole && (__builtin_popcountll(chunks) < CHECK_FLUSH_MAX_LISTS))
        check_flush_chunk_lists(block_chunk_lists[lists_nr].head[1], chunks, 1);
    else {
        block_nr = page->block_2;

        while (block_nr) {
            codeblock_t *block      = &codeblock[block_nr];
            uint32_t     next_block = block->next_2;

            check_flush_block_2(block);
#ifndef RELEASE_BUILD
            if (block_nr == next_block)
                fatal("Broken 2\n");
#endif
            block_nr = next_block;
        }
    }

    page_store_fast_clear(page);
    if (page->code_present_mask & page->dirty_mask)
        remove_from_evict_list = 1;
    page->code_present_mask &= ~page->dirty_mask;
    page->dirty_mask = 0;

    /* A page with no RAM behind it has no byte masks of its own */
    if (page->mem != page_ff) {
        /* Locals: the stores below could otherwise alias the page's pointers,
           reloading them for every word. */
        uint64_t *restrict code_present = page->byte_code_present_mask;
        uint64_t *restrict dirty        = page->byte_dirty_mask;
        /* Only the byte mask words that hold a dirty bit: chunks has them
           all, unless the whole lists were walked. */
        uint64_t words = whole ? UINT64_MAX : chunks;

        while (words) {
            int      c = __builtin_ctzll(words);
            uint64_t d = dirty[c];

            words &= words - 1;
            if (!d)
                continue;
            if (code_present[c] & d)
                remove_from_evict_list = 0;
            code_present[c] &= ~d;
            dirty[c] = 0;
        }
    }
    if (remove_from_evict_list)
        page_remove_from_evict_list(page);
}

void
codegen_block_init(uint32_t phys_addr)
{
    codeblock_t *block;
    page_t      *page = &pages[phys_addr >> 12];

    if (!page->block)
        mem_flush_write_page(phys_addr, cs + cpu_state.pc);
    block = block_free_list_get();
#ifndef RELEASE_BUILD
    if (!block)
        fatal("codegen_block_init: block_free_list_get() returned NULL\n");
#endif
    block_current = get_block_nr(block);

    block_num                 = HASH(phys_addr);
    codeblock_hash[block_num] = block_current;
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    if (block == block_checked_cooldown_block)
        block_checked_cooldown_block = NULL;
#endif

    block->valid             = 1;
    block->ins               = 0;
    block->smc_strikes        = 0;
    block->smc_family_score   = 0;
    block_family_hold[block_current] = 0;
    block_family_runs[block_current] = 0;
    block_jcc_hot_tries[block_current] = 0;
    block_self_check_count[block_current] = 0;
    /* On a page whose blocks keep being rewritten before they pay for
       compiling, a new one is likely to be too: interpret it for a while
       first. One that pays for itself later takes a strike off the page. */
    block->smc_cooldown_until = smc_thrash_new_block_cooldown(page->smc_strikes, tsc);
    block->pc                = cs + cpu_state.pc;
    block->_cs         = cs;
    block->phys        = phys_addr;
    block->dirty_mask  = &page->dirty_mask;
    block_chk_dirty_sync(block);
    block->dirty_mask2 = NULL;
    block->next = block->prev = BLOCK_INVALID;
    block->next_2 = block->prev_2 = BLOCK_INVALID;
    block->page_mask = block->page_mask2 = 0;
    block->page2_delta                   = 1;
    block->flags                         = CODEBLOCK_STATIC_TOP;
    block->status                        = cpu_cur_status;
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#endif

    recomp_page  = block->phys & ~0xfff;
    recomp_page2 = -1;
    codeblock_tree_add(block);
}

static ir_data_t *ir_data;

ir_data_t *
codegen_get_ir_data(void)
{
    return ir_data;
}

/* Checks, on entry to the block, that the code lines it was marked over are
   among those fetched lately (cpu_code_line_tags); one that is not costs a
   memory read (cpu_code_line_miss()). Compiled code fetches nothing through
   the slow paths that touch the lines, so without this a compiled block would
   run free however long ago its code was last fetched. That matters only
   while the block cache thrashes (codegen_admit()): with more code going
   round than the cache holds compiled, far more than any processor cache
   holds, a block runs a few times before it is thrown out, each time from
   cold. Otherwise the checks would only make compiled code bigger and
   slower, so blocks get them only then; the reset that ends the thrash
   phase drops them all. The block's lines are those of its 64-byte chunks
   in the marking pass's page masks; a byte-mask block's masks are bytes of
   its chunk and the next one. */
#define CODEGEN_CODE_LINES_MAX 16

static void
codegen_code_lines_check(ir_data_t *ir, const codeblock_t *block, uint64_t mask, uint64_t mask2, int byte_mask)
{
    uint32_t lines[CODEGEN_CODE_LINES_MAX];
    int      nr = 0;

    if (!cpu_code_miss_cycles || (codegen_evict_thrash < CODEGEN_ADMIT_THRASH))
        return;

    if (byte_mask) {
        lines[nr++] = block->pc >> CPU_CODE_LINE_SHIFT;
        if (mask2)
            lines[nr++] = (block->pc >> CPU_CODE_LINE_SHIFT) + 1;
    } else {
        const uint32_t base = (block->pc & ~0xfff) >> CPU_CODE_LINE_SHIFT;

        if (!mask && !mask2)
            mask = (uint64_t) 1 << ((block->pc >> CPU_CODE_LINE_SHIFT) & 63);
        for (int c = 0; (c < 64) && (nr < CODEGEN_CODE_LINES_MAX); c++) {
            if (mask & ((uint64_t) 1 << c))
                lines[nr++] = base + c;
        }
        for (int c = 0; (c < 64) && (nr < CODEGEN_CODE_LINES_MAX); c++) {
            if (mask2 & ((uint64_t) 1 << c))
                lines[nr++] = base + 64 + c;
        }
    }

    for (int c = 0; c < nr; c++) {
        const uint32_t tag = lines[c] + 1;
        int            jump_uop;

        uop_MOV_REG_PTR(ir, IREG_temp0, &cpu_code_line_tags[tag & (CPU_CODE_LINES - 1)]);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, tag);
        uop_LOAD_FUNC_ARG_IMM(ir, 0, tag);
        uop_CALL_FUNC(ir, cpu_code_line_miss);
        uop_NOP_BARRIER(ir);
        uop_set_jump_dest(ir, jump_uop);
    }
}

void
codegen_block_start_recompile(codeblock_t *block)
{
    page_t        *page            = &pages[block->phys >> 12];
    const uint64_t mark_page_mask  = block->page_mask;
    const uint64_t mark_page_mask2 = block->page_mask2;

    if (codegen_evict_thrash && (--codegen_evict_thrash == (CODEGEN_ADMIT_THRASH - 1)))
        codegen_reset_pending = 1;

    if (!page->block)
        mem_flush_write_page(block->phys, cs + cpu_state.pc);

    block_num     = HASH(block->phys);
    block_current = get_block_nr(block); // block->pnt;

#ifndef RELEASE_BUILD
    if (block->pc != cs + cpu_state.pc || (block->flags & CODEBLOCK_WAS_RECOMPILED))
        fatal("Recompile to used block!\n");
#endif

#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    if (block == block_checked_cooldown_block) {
        block->smc_cooldown_until    = block_checked_cooldown;
        block_checked_cooldown_block = NULL;
    }
    /*Writes leave the dirty bits over a checked block's bytes set. Cleared
      first, as the page of any rewritten block is before it is compiled, so
      that a write the compile pass makes to them is seen as it is for any
      block (codegen_block_end_recompile()). Not for one still on the dirty
      list: the dispatcher flushed its page as for any block there, and a
      flush may evict it from the list.*/
    if (((block->flags & (CODEBLOCK_CHECKED | CODEBLOCK_IN_DIRTY_LIST)) == CODEBLOCK_CHECKED) && block_checked_written(block))
        codegen_check_flush(page, page->dirty_mask, block->phys);
#endif
    if (block_version_cur[block_current]) {
        block_version_free(block_version_cur[block_current]);
        block_version_cur[block_current] = 0;
    }
    /*Only blocks whose second chunk is in the same page get versions: a
      mapping change on the next page can drop page_mask2.*/
    block_version_start_ok = 0;
    if ((block->flags & CODEBLOCK_BYTE_MASK) && (block->flags & CODEBLOCK_NO_IMMEDIATES) && ((block->pc & 0xfff) < 0xfc0)) {
        block_version_start_exec   = block_version_read_chunk(block->phys, block_version_start_code);
        block_version_start_phys_2 = block->phys + 0x40;
        block_version_start_exec_2 = block_version_read_chunk(block_version_start_phys_2, &block_version_start_code[64]);
        block_version_start_cs     = CS;
        block_version_start_ok     = (block_version_start_exec != NULL);
    }
    if (block->head_mem_block) {
        codegen_allocator_free(block->head_mem_block);
        block->head_mem_block = NULL;
    }
    block->head_mem_block = codegen_allocator_allocate(NULL, block_current);
    block->data           = codeblock_allocator_get_ptr(block->head_mem_block);

    /* The NOTFLAT bits stay as the block was marked: a visit with a segment
       flat may compile a block marked with it not flat, and narrowing the
       bits would leave every later non-flat visit a new block. */
    block->status = (cpu_cur_status & ~CPU_STATUS_MASK) | (block->status & CPU_STATUS_MASK);
    block->used   = 1; /* new: not the next thing the eviction sweep takes */
    block->runs_left = CODEBLOCK_RUNS_NEW; /* not yet run from block->data */

    block->page_mask = block->page_mask2 = 0;
    block->page2_delta                   = 1;
    block->ins                           = 0;
    codegen_family_ops[0] = codegen_family_ops[1] = 0;
    codegen_trace_reset(block);

    cpu_block_end = 0;

    codegen_block_cycles = 0;
    codegen_timing_block_start();

    recomp_page  = block->phys & ~0xfff;
    recomp_page2 = -1;

    codegen_flags_changed = 0;
    codegen_flags_known_reset();
    codegen_fpu_entered   = 0;
    codegen_fpu_es_check  = 0;
    codegen_mmx_entered   = 0;
    codegen_fpu_class_dirty = 0;

    codegen_fpu_loaded_iq[0] = codegen_fpu_loaded_iq[1] = codegen_fpu_loaded_iq[2] = codegen_fpu_loaded_iq[3] = codegen_fpu_loaded_iq[4] = codegen_fpu_loaded_iq[5] = codegen_fpu_loaded_iq[6] = codegen_fpu_loaded_iq[7] = 0;

    cpu_state.seg_ds.checked = cpu_state.seg_es.checked = cpu_state.seg_fs.checked = cpu_state.seg_gs.checked = (cr0 & 1) ? 0 : 1;

    block->TOP = cpu_state.TOP & 7;
    block->flags = (block->flags & ~CODEBLOCK_OPCODE_FAMILY) | CODEBLOCK_WAS_RECOMPILED;
    codegen_page2_walked     = 0;
    codegen_fetch_walk_off   = 0;
    codegen_fetch_walk_defer = 0;

    codegen_flat_ds = !(block->status & CPU_STATUS_NOTFLATDS);
    codegen_flat_ss = !(block->status & CPU_STATUS_NOTFLATSS);
    codegen_flat_es = !(block->status & CPU_STATUS_NOTFLATES);
    codegen_wide_ds = !(block->status & CPU_STATUS_NOTWIDEDS);
    codegen_wide_ss = !(block->status & CPU_STATUS_NOTWIDESS);
    codegen_v86_64k = (block->status & CPU_STATUS_V86) && !(block->status & CPU_STATUS_NOT64K);

    if (block->flags & CODEBLOCK_BYTE_MASK) {
        block->dirty_mask  = &page->byte_dirty_mask[(block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK];
        block_chk_dirty_sync(block);
        block->dirty_mask2 = NULL;
    }
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
    link_slots_clear(block);
#endif

    ir_data        = codegen_ir_init();
    ir_data->block = block;
    codegen_reg_reset();
    codegen_accumulate_reset();
    codegen_generate_reset();
    codegen_fwd_reset();

    codegen_code_lines_check(ir_data, block, mark_page_mask, mark_page_mask2, block->flags & CODEBLOCK_BYTE_MASK);
}

void
codegen_block_remove(void)
{
    codeblock_t *block = &codeblock[block_current];

    delete_block(block);

    recomp_page  = -1;
    recomp_page2 = -1;
}

void
codegen_block_generate_end_mask_recompile(void)
{
    codeblock_t *block = &codeblock[block_current];
    page_t      *p;
    /*Writes do not mark a block whose current code is a version with the
      entry check: that compares its bytes on every entry instead*/
    const int untracked = block_version_cur[block_current] && block_versions[block_version_cur[block_current]].checked;

    p = &pages[block->phys >> 12];
    page_store_fast_clear(p);
    if (block->flags & CODEBLOCK_BYTE_MASK) {
        int offset = (block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

        /*Marked as code either way: devices that write RAM directly mark
          only bytes marked as code (mem_phys_has_code())*/
        p->byte_code_present_mask[offset] |= block->page_mask;
        block->dirty_mask = untracked ? &block_untracked_mask : &p->byte_dirty_mask[offset];
        block_chk_dirty_sync(block);
    } else
        p->code_present_mask |= block->page_mask;

    if ((*(block->dirty_mask) & block->page_mask) && !page_in_evict_list(p))
        page_add_to_evict_list(p);

    block->phys_2 = -1;
    block->next_2 = block->prev_2 = BLOCK_INVALID;
    if (block->page_mask2) {
        /*The second chunk or page is the one the lookup checks
          (386_dynarec.c): the next 64 bytes of a byte-mask block, else the
          page codeblock_pc_2() names. Not codegen_endpc, which guesses where the last
          instruction ends from where it starts: one of 11 bytes or more
          marks bytes past the guess, in a chunk or page it doesn't name.*/
        uint32_t pc_2 = codeblock_pc_2(block);

        block->phys_2 = get_phys_noabrt(pc_2);
        if (block->phys_2 != -1) {
            page_t *page_2 = &pages[block->phys_2 >> 12];

            page_store_fast_clear(page_2);
            if (block->flags & CODEBLOCK_BYTE_MASK) {
                int offset = (block->phys_2 >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

                page_2->byte_code_present_mask[offset] |= block->page_mask2;
                block->dirty_mask2 = untracked ? &block_untracked_mask : &page_2->byte_dirty_mask[offset];
            } else {
                page_2->code_present_mask |= block->page_mask2;
                block->dirty_mask2 = &page_2->dirty_mask;
            }
            if (((*block->dirty_mask2) & block->page_mask2) && !page_in_evict_list(page_2))
                page_add_to_evict_list(page_2);

            if (!pages[block->phys_2 >> 12].block_2)
                mem_flush_write_page(block->phys_2, pc_2);

#ifndef RELEASE_BUILD
            if (!block->page_mask2)
                fatal("!page_mask2\n");
            if (block->next_2) {
                if (!codeblock[block->next_2].valid)
                    fatal("!block->next_2->valid %p\n", (void *) &codeblock[block->next_2]);
            }
#endif
        } else {
            /*Second page not present. page_mask2 is most likely set only because
              the recompiler didn't know how long the last instruction was, so
              clear it*/
            block->page_mask2 = 0;
        }
    }

    recomp_page  = -1;
    recomp_page2 = -1;
}

void
codegen_block_generate_end_mask_mark(void)
{
    codeblock_t *block = &codeblock[block_current];
    uint32_t     start_pc;
    uint32_t     end_pc;
    page_t      *p;

#ifndef RELEASE_BUILD
    if (block->flags & CODEBLOCK_BYTE_MASK)
        fatal("codegen_block_generate_end_mask2() - BYTE_MASK\n");
#endif

    block->page_mask = 0;
    start_pc         = (block->pc & 0xfff) & ~63;
    if ((block->pc ^ codegen_endpc) & ~0xfff)
        end_pc = 0xfff & ~63;
    else
        end_pc = (codegen_endpc & 0xfff) & ~63;
    if (end_pc < start_pc)
        end_pc = 0xfff;
    start_pc >>= PAGE_MASK_SHIFT;
    end_pc >>= PAGE_MASK_SHIFT;

    for (; start_pc <= end_pc; start_pc++) {
        block->page_mask |= ((uint64_t) 1 << start_pc);
    }

    p = &pages[block->phys >> 12];
    page_store_fast_clear(p);
    p->code_present_mask |= block->page_mask;
    if ((p->dirty_mask & block->page_mask) && !page_in_evict_list(p))
        page_add_to_evict_list(p);

    block->phys_2      = -1;
    block->page_mask2  = 0;
    block->page2_delta = 1;
    block->next_2 = block->prev_2 = BLOCK_INVALID;
    if ((block->pc ^ codegen_endpc) & ~0xfff) {
        block->phys_2 = get_phys_noabrt(codegen_endpc);
        if (block->phys_2 != -1) {
            page_t *page_2 = &pages[block->phys_2 >> 12];

            start_pc = 0;
            end_pc   = (codegen_endpc & 0xfff) >> PAGE_MASK_SHIFT;
            for (; start_pc <= end_pc; start_pc++)
                block->page_mask2 |= ((uint64_t) 1 << start_pc);

            page_store_fast_clear(page_2);
            page_2->code_present_mask |= block->page_mask2;
            if ((page_2->dirty_mask & block->page_mask2) && !page_in_evict_list(page_2))
                page_add_to_evict_list(page_2);

            if (!pages[block->phys_2 >> 12].block_2)
                mem_flush_write_page(block->phys_2, codegen_endpc);

#ifndef RELEASE_BUILD
            if (!block->page_mask2)
                fatal("!page_mask2\n");
            if (block->next_2) {
                if (!codeblock[block->next_2].valid)
                    fatal("!block->next_2->valid %p\n", (void *) &codeblock[block->next_2]);
            }
#endif
            block->dirty_mask2 = &page_2->dirty_mask;
        } else {
            /*Second page not present. page_mask2 is most likely set only because
              the recompiler didn't know how long the last instruction was, so
              clear it*/
            block->page_mask2 = 0;
        }
    }

    recomp_page  = -1;
    recomp_page2 = -1;
}

void
codegen_block_end(void)
{
    codeblock_t *block = &codeblock[block_current];

    codegen_block_generate_end_mask_mark();
    add_to_block_list(block);
}

#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
/*A checked block's compile records its version before the code is
  generated, for the entry check to compare with, on the terms any version is
  recorded on: the bytes and mapping as at the start of the compile, if they
  are the same now and none of its code bytes is dirty. Its dirty pointers
  are already the untracked ones here, so the bits are read from the page,
  whose two chunks hold both of the block's (codegen_block_start_recompile()
  cleared them).*/
static void
block_version_checked_start(codeblock_t *block, uint64_t page_mask2_compiled)
{
    const uint64_t *dirty = &pages[block->phys >> 12].byte_dirty_mask[(block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK];
    block_version_t *v;
    uint16_t         nr;

    if (!block_version_start_ok || (block->page_mask2 != page_mask2_compiled) ||
        (dirty[0] & block->page_mask) || (block->page_mask2 && (dirty[1] & block->page_mask2)) ||
        !block_version_chunk_matches(block->phys, block_version_start_exec, block->page_mask, block_version_start_code) ||
        (block->page_mask2 &&
         ((block->phys_2 != block_version_start_phys_2) ||
          !block_version_chunk_matches(block->phys_2, block_version_start_exec_2, block->page_mask2, &block_version_start_code[64]))))
        return;
    nr = block_version_alloc();
    if (!nr)
        return;
    v                 = &block_versions[nr];
    v->exec           = block_version_start_exec;
    v->exec_2         = block->page_mask2 ? block_version_start_exec_2 : NULL;
    memcpy(v->code, block_version_start_code, sizeof(v->code));
    v->cs_sel         = block_version_start_cs;
    v->head_mem_block = block->head_mem_block;
    v->page_mask      = block->page_mask;
    v->page_mask2     = block->page_mask2;
    v->phys_2         = block->phys_2;
    v->owner          = get_block_nr(block);
    v->status         = block->status;
    v->TOP            = block->TOP;
    v->checked        = 1;
    block_version_chk_fill(v);

    codegen_entry_check.dirty         = dirty;
    codegen_entry_check.dirty_bits[0] = block->page_mask;
    codegen_entry_check.dirty_bits[1] = block->page_mask2;
    codegen_entry_check.exec_slot = &_mem_exec[block->phys >> MEM_GRANULARITY_BITS];
    codegen_entry_check.exec      = v->exec;
    codegen_entry_check.offset    = (block->phys & MEM_GRANULARITY_MASK) & ~0x3f;
    codegen_entry_check.word      = v->chk_word;
    codegen_entry_check.chk       = v->chk;
    codegen_entry_check.nr        = v->chk_nr;
    block_version_checked_nr      = nr;
}
#endif
/*Version v matches the block's bytes and the CPU state*/
static int
block_version_matches(const codeblock_t *block, const block_version_t *v)
{
    if ((v->cs_sel != CS) || ((v->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) ||
        ((v->status & cpu_cur_status & CPU_STATUS_MASK) != (cpu_cur_status & CPU_STATUS_MASK)) ||
        ((v->flags & CODEBLOCK_STATIC_TOP) && (v->TOP != (cpu_state.TOP & 7))) ||
        !block_version_chunk_matches(block->phys, v->exec, v->page_mask, v->code))
        return 0;
    return !v->page_mask2 || ((v->phys_2 == (block->phys + 0x40)) &&
                              block_version_chunk_matches(v->phys_2, v->exec_2, v->page_mask2, &v->code[64]));
}

/*Takes the kept version of the block matching its bytes and the CPU state,
  with none of the flags in skip, and with one_chunk on one 64-byte chunk,
  out of the kept list; 0 if there is none*/
static uint16_t
block_version_take(const codeblock_t *block, uint16_t skip, int one_chunk)
{
    uint32_t  block_nr = get_block_nr(block);
    uint16_t *link     = &block_version_kept[block_nr];

    while (*link) {
        uint16_t         nr = *link;
        block_version_t *v  = &block_versions[nr];

        if ((v->flags & skip) || (one_chunk && v->page_mask2) || !block_version_matches(block, v)) {
            link = &v->next;
            continue;
        }

        *link   = v->next;
        v->next = 0;
        block_version_kept_nr--;
        return nr;
    }
    return 0;
}

/*Kept version nr, out of the kept list, becomes the block's current code, as
  a compile would make it. The block is in the dirty list*/
static void
block_version_apply(codeblock_t *block, uint16_t nr)
{
    uint32_t         block_nr = get_block_nr(block);
    page_t          *page     = &pages[block->phys >> 12];
    block_version_t *v        = &block_versions[nr];

    v->state                    = BLOCK_VERSION_CURRENT;
    block_version_cur[block_nr] = nr;

    if (!page->block)
        mem_flush_write_page(block->phys, cs + cpu_state.pc);
    block_current = block_nr;
    block_dirty_list_remove(block);
    block->next = block->prev = BLOCK_INVALID;
    block->next_2 = block->prev_2 = BLOCK_INVALID;

    block->head_mem_block = v->head_mem_block;
    block->data           = codeblock_allocator_get_ptr(v->head_mem_block);
    block->page_mask      = v->page_mask;
    block->page_mask2     = v->page_mask2;
    block->page2_delta    = 1;
    block->status         = v->status;
    block->TOP            = v->TOP;
    block->dirty_mask     = &page->byte_dirty_mask[(block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK];
    block->dirty_mask2    = NULL;
    block->flags          = (block->flags & ~BLOCK_VERSION_FLAGS) | v->flags | CODEBLOCK_WAS_RECOMPILED;
    /* Restoring a kept version is cheap (a few memcmp calls), unlike a
       real recompile: do not let it count as an SMC-thrash strike if
       it gets invalidated again before being run often from block->data.
       It still counts as a short run for the opcode family
       (smc_family_rewritten()). */
    block->runs_left      = CODEBLOCK_RUNS_PAID;
    codegen_block_generate_end_mask_recompile();
    add_to_block_list(block);
    block_version_owner_sync(block_nr);
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#endif
    if (v->flags & CODEBLOCK_OPCODE_FAMILY)
        family_review_add(block_nr);
}

/*The opcode bytes are left as they are, unmarked: the compile pass marks a
  byte out of the family as it compiles it, while those of the family stay
  read at run time. Code that flips a byte between an opcode of the family and
  one out of it has the version compiled with the latter kept: put it back,
  keeping this one for when the byte is flipped back (which dirties a byte
  that version marks). Only a version that reads no opcode as it runs, which
  cannot miss in turn*/
int
codegen_opcode_family_miss(uint32_t block_nr)
{
    codeblock_t *block = &codeblock[block_nr];
    uint16_t     nr    = block_version_take(block, CODEBLOCK_OPCODE_FAMILY, 0);

    if (nr) {
        invalidate_block(block);
        block_version_apply(block, nr);
        return 1;
    }
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    /* Covers the flag write just below. */
    codegen_link_block_changed(block);
#endif
    block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
    return 1;
}

void
codegen_block_end_recompile(codeblock_t *block)
{
    uint64_t page_mask2_compiled = block->page_mask2;
    int      drain_from;

    codegen_fwd_end(cs + cpu_state.pc, cs + cpu_state.oldpc);
    codegen_accumulate_record(ACC_END, 0, 0, 0, 0, 0);
    drain_from = codegen_block_cycles;
    codegen_timing_block_end();
    codegen_exit_end_charge_apply(ir_data, codegen_block_cycles - drain_from);
    codegen_accumulate_timing(ir_data);

    if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
        block_dirty_list_remove(block);
    else
        remove_from_block_list(block, block->pc);
    block->next = block->prev = BLOCK_INVALID;
    block->next_2 = block->prev_2 = BLOCK_INVALID;
    codegen_block_generate_end_mask_recompile();
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    block_version_checked_nr = 0;
    if (block->flags & CODEBLOCK_CHECKED) {
        /*Without a version its code has nothing to compare with and stays
          marked by writes, as codegen_block_generate_end_mask_recompile()
          left it*/
        block_version_checked_start(block, page_mask2_compiled);
        if (block_version_checked_nr) {
            block->dirty_mask = &block_untracked_mask;
            block_chk_dirty_sync(block);
            if (block->dirty_mask2)
                block->dirty_mask2 = &block_untracked_mask;
        }
    }
#endif
    add_to_block_list(block);
    block_self_check_count[get_block_nr(block)] = 0;

    if (!(block->flags & CODEBLOCK_HAS_FPU))
        block->flags &= ~CODEBLOCK_STATIC_TOP;

    /*The unrolled branch falls through when taken. Only its not-taken
      exit owes the drain and the branch credit.*/
    if (!codegen_ir_unrolling())
        codegen_accumulate_flush(ir_data);
    codegen_ir_compile(ir_data, block);
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#endif

#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    if (block_version_checked_nr) {
        block_version_t *v = &block_versions[block_version_checked_nr];

        codegen_entry_check.nr                  = 0;
        v->flags                                = block->flags & BLOCK_VERSION_FLAGS;
        v->state                                = BLOCK_VERSION_CURRENT;
        block_version_cur[get_block_nr(block)] = block_version_checked_nr;
        return;
    }
#endif

    /*Record what this version was compiled from, so it can be kept when the
      guest rewrites the block: the bytes and mappings as at the start of the
      compile, if they are the same now and none of its code bytes is dirty
      (the block may have rewritten them as it ran here).*/
    if (block_version_start_ok && (block->page_mask2 == page_mask2_compiled) &&
        (block->flags & CODEBLOCK_BYTE_MASK) && (block->flags & CODEBLOCK_NO_IMMEDIATES) &&
        !(*block->dirty_mask & block->page_mask) &&
        !(block->page_mask2 && (*block->dirty_mask2 & block->page_mask2))) {
        uint16_t nr = block_version_alloc();

        if (nr) {
            block_version_t *v = &block_versions[nr];

            v->exec   = block_version_start_exec;
            v->exec_2 = (block->page_mask2 || codegen_family_ops[1]) ? block_version_start_exec_2 : NULL;
            memcpy(v->code, block_version_start_code, sizeof(v->code));
            if (block_version_chunk_matches(block->phys, v->exec, block->page_mask, v->code) &&
                (!block->page_mask2 ||
                 ((block->phys_2 == block_version_start_phys_2) &&
                  block_version_chunk_matches(block->phys_2, v->exec_2, block->page_mask2, &v->code[64])))) {
                v->cs_sel                             = block_version_start_cs;
                v->head_mem_block                     = block->head_mem_block;
                v->page_mask                          = block->page_mask;
                v->page_mask2                         = block->page_mask2;
                v->family_ops[0]                      = codegen_family_ops[0];
                v->family_ops[1]                      = codegen_family_ops[1];
                v->phys_2                             = block->phys_2;
                v->owner                              = get_block_nr(block);
                v->status                             = block->status;
                v->flags                              = block->flags & BLOCK_VERSION_FLAGS;
                v->TOP                                = block->TOP;
                v->checked                            = 0;
                v->state                              = BLOCK_VERSION_CURRENT;
                block_version_cur[get_block_nr(block)] = nr;
                if (block->flags & CODEBLOCK_OPCODE_FAMILY)
                    family_review_add(get_block_nr(block));
            } else
                block_version_free(nr);
        }
    }
}

/*Put back a kept version of a block in the dirty list whose code bytes are in
  guest memory again. Returns 1 if the block now holds compiled code.*/
int
codegen_block_restore_version(codeblock_t *block)
{
    /* A version that reads its opcodes at run time reads none of those
       bytes, so it always matches: only put one back while the score says
       the block's opcodes change every few runs (smc_family_ready()), and
       not while it is off one to try whether they still do
       (smc_family_hold()). Else compile it for the bytes it has. */
    int      skip_family = !smc_family_ready(block->smc_family_score) || (block_family_hold[get_block_nr(block)] & SMC_FAMILY_HOLD_TRIED);
    uint16_t nr          = block_version_take(block, skip_family ? CODEBLOCK_OPCODE_FAMILY : 0, 0);

    if (!nr)
        return 0;
    block_version_apply(block, nr);
    /* Not a block on two chunks, which a write to the second page must
       still mark, nor one reading its opcodes as it runs, which matches
       whatever they are, nor a checked one, which its entry check covers. */
    if (!block->page_mask2 && !(block->flags & (CODEBLOCK_OPCODE_FAMILY | CODEBLOCK_CHECKED)) &&
        (++block_self_check_count[get_block_nr(block)] >= SELF_CHECK_ENTER)) {
        /* No slot links to it: block_version_apply() changed its link_gen,
           and codegen_link_patch() links no block with the flag. */
        remove_from_block_list(block, block->pc);
        block->flags |= CODEBLOCK_SELF_CHECK;
        block_self_check_count[get_block_nr(block)] = 0;
    }
    return 1;
}

/*Called by the dispatcher each time a block with CODEBLOCK_SELF_CHECK is
  entered. Returns 1 if the block holds compiled code matching its bytes, to
  be run. Else the block is back on its page's list, as a block that was
  never off it would be, for the dispatcher to go on with as it does with
  any block that failed its checks; it may be invalid (no longer valid).*/
int
codegen_block_self_check(codeblock_t *block)
{
    const uint32_t block_nr = get_block_nr(block);
    const uint16_t cur      = block_version_cur[block_nr];
    int            same     = 0;

    /* Not when a hot loop exit cleared CODEBLOCK_WAS_RECOMPILED
       (codegen_jcc_hot_exit()): that wants it compiled again. */
    if (block->flags & CODEBLOCK_WAS_RECOMPILED) {
        if (cur && (block_versions[cur].head_mem_block == block->head_mem_block) &&
            block_version_matches(block, &block_versions[cur])) {
            if (++block_self_check_count[block_nr] < SELF_CHECK_STABLE)
                return 1;
            same = 1;
        } else {
            uint16_t nr = block_version_take(block, CODEBLOCK_OPCODE_FAMILY, 1);

            if (nr) {
                page_t          *page = &pages[block->phys >> 12];
                block_version_t *v    = &block_versions[nr];

                /* As block_version_apply(), but the block stays off the
                   lists. Its slots stay: each is checked against the exit
                   it is taken from. */
                block_version_keep_cur(block);
                v->state                    = BLOCK_VERSION_CURRENT;
                block_version_cur[block_nr] = nr;
                block->head_mem_block       = v->head_mem_block;
                block->data                 = codeblock_allocator_get_ptr(v->head_mem_block);
                block->page_mask            = v->page_mask;
                block->status               = v->status;
                block->TOP                  = v->TOP;
                block->flags                = (block->flags & ~BLOCK_VERSION_FLAGS) | v->flags | CODEBLOCK_WAS_RECOMPILED;
                block->runs_left            = CODEBLOCK_RUNS_PAID;
                page->byte_code_present_mask[(block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK] |= block->page_mask;
                block_version_owner_sync(block_nr);
                block_self_check_count[block_nr] = 0;
                return 1;
            }
        }
    }

    block->flags &= ~CODEBLOCK_SELF_CHECK;
    block_self_check_count[block_nr] = 0;
    /* With no block listed on the page, writes to it went straight to
       memory: make them mark it again, as codegen_block_init() does. */
    if (!pages[block->phys >> 12].block)
        mem_flush_write_page(block->phys, cs + cpu_state.pc);
    add_to_block_list(block);
    if (!(block->flags & CODEBLOCK_WAS_RECOMPILED))
        return 0;
    /* Dirty bits of its bytes from writes while it was off the list: the
       dispatcher's flush takes it, as any block they cover. */
    if (block->page_mask & *block->dirty_mask)
        return 0;
    if (same)
        return 1;
    /* Its bytes were rewritten with nothing marking it */
    invalidate_block(block);
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    /* Covers the flag write just below. */
    codegen_link_block_changed(block);
#endif
    block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
    return 0;
}

/*Bytes of a block's code that differ from its newest kept version, counted
  only up to the limit*/
static int
block_version_chunk_diff(uint32_t phys, const uint8_t *exec, uint64_t mask, const uint8_t *code, int limit)
{
    const uint8_t *fetch = _mem_exec[phys >> MEM_GRANULARITY_BITS];
    int            n     = 0;

    if (!fetch || (fetch != exec))
        return limit + 1;
    fetch = &fetch[(phys & MEM_GRANULARITY_MASK) & ~0x3f];
    for (int c = 0; mask && (n <= limit); c++, mask >>= 1)
        n += (mask & 1) && (fetch[c] != code[c]);
    return n;
}

/*For a block off the dirty list that is about to be compiled again, no kept
  version having matched: 1 if its code differs from the newest kept one in
  only a few bytes. Such a block was patched where it is (an operand, a
  branch), and is run again right away as it was before. Else the address
  likely holds other code now: that gets the cooldown a new block on its page
  would (codegen_block_init()).*/
int
codegen_block_dirty_patched(const codeblock_t *block)
{
    const uint16_t         nr = block_version_kept[get_block_nr(block)];
    const block_version_t *v;
    int                    n;

    if (!nr)
        return 0;
    /* Over the bytes that version was compiled from: a patch can change
       where the block ends. */
    v = &block_versions[nr];
    n = block_version_chunk_diff(block->phys, v->exec, v->page_mask, v->code, SMC_PATCH_MAX_BYTES);
    if (v->page_mask2 && (n <= SMC_PATCH_MAX_BYTES))
        n += block_version_chunk_diff(v->phys_2, v->exec_2, v->page_mask2, &v->code[64], SMC_PATCH_MAX_BYTES - n);
    return n <= SMC_PATCH_MAX_BYTES;
}

#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
/*Keep a checked block's current version, newest first, and no more than
  BLOCK_VERSION_PER_BLOCK, as invalidate_block() keeps it*/
static void
block_version_keep_current(codeblock_t *block)
{
    uint32_t block_nr = get_block_nr(block);
    uint16_t cur      = block_version_cur[block_nr];

    if (cur) {
        int      c = 1;
        uint16_t nr;

        block_versions[cur].state    = BLOCK_VERSION_KEPT;
        block_version_kept_nr++;
        block_versions[cur].next     = block_version_kept[block_nr];
        block_version_kept[block_nr] = cur;
        for (nr = cur; block_versions[nr].next; nr = block_versions[nr].next) {
            if (++c > BLOCK_VERSION_PER_BLOCK) {
                uint16_t oldest             = block_versions[nr].next;
                block_versions[nr].next     = 0;
                block_versions[oldest].next = 0;
                block_version_free(oldest);
                break;
            }
        }
        block_version_cur[block_nr] = 0;
    } else if (block->head_mem_block)
        codegen_allocator_free_version(block->head_mem_block);
    block->head_mem_block = NULL;
    block_version_owner_sync(block_nr);
}

/*Put back the kept version of a checked block whose bytes are in guest
  memory, on the terms and from the versions codegen_block_restore_version()
  puts one back from: one compiled before the block was checked has no entry
  check and is marked by writes again. Returns 1 if one was.*/
static int
block_version_put_back(codeblock_t *block)
{
    uint32_t  block_nr = get_block_nr(block);
    uint16_t *link     = &block_version_kept[block_nr];
    page_t   *page     = &pages[block->phys >> 12];

    while (*link) {
        uint16_t         nr = *link;
        block_version_t *v  = &block_versions[nr];

        if ((v->cs_sel != CS) || ((v->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) ||
            ((v->status & cpu_cur_status & CPU_STATUS_MASK) != (cpu_cur_status & CPU_STATUS_MASK)) ||
            ((v->flags & CODEBLOCK_STATIC_TOP) && (v->TOP != (cpu_state.TOP & 7))) ||
            !block_version_chunk_matches(block->phys, v->exec, v->page_mask, v->code)) {
            link = &v->next;
            continue;
        }
        if (v->page_mask2 && ((v->phys_2 != (block->phys + 0x40)) ||
                              !block_version_chunk_matches(v->phys_2, v->exec_2, v->page_mask2, &v->code[64]))) {
            link = &v->next;
            continue;
        }

        *link                       = v->next;
        v->next                     = 0;
        v->state                    = BLOCK_VERSION_CURRENT;
        block_version_cur[block_nr] = nr;
        block_version_kept_nr--;

        block_current = block_nr;
        remove_from_block_list(block, block->pc);
        if (!page->block)
            mem_flush_write_page(block->phys, cs + cpu_state.pc);
        block->next = block->prev = BLOCK_INVALID;
        block->next_2 = block->prev_2 = BLOCK_INVALID;

        block->head_mem_block = v->head_mem_block;
        block->data           = codeblock_allocator_get_ptr(v->head_mem_block);
        block->page_mask      = v->page_mask;
        block->page_mask2     = v->page_mask2;
        block->status         = v->status;
        block->TOP            = v->TOP;
        block->dirty_mask2    = NULL;
        block->flags          = (block->flags & ~BLOCK_VERSION_FLAGS) | v->flags | CODEBLOCK_WAS_RECOMPILED;
        block->runs_left      = CODEBLOCK_RUNS_PAID;
        if (block == block_checked_cooldown_block) {
            block->smc_cooldown_until    = block_checked_cooldown;
            block_checked_cooldown_block = NULL;
        }
        codegen_block_generate_end_mask_recompile();
        add_to_block_list(block);
        block_version_owner_sync(block_nr);
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
        codegen_link_block_changed(block);
#    endif
        return 1;
    }
    return 0;
}

/*A checked block whose bytes were written, as invalidate_block() handles a
  block marked by writes, but not put on the dirty list, whose evictions would
  take its mode and its versions with it: the strike counted and its current
  version kept, leaving it without code. Its next entry
  (codegen_block_check()) puts back the kept version compiled from the bytes
  there now or compiles it again where it is, as the loop does with a block
  on the dirty list. The cooldown the strike sets on the block the loop is
  entering starts with that, as the loop tests the cooldown before
  invalidate_block() sets it.*/
static void
block_checked_invalidate(codeblock_t *block)
{
    uint64_t cooldown = block->smc_cooldown_until;

    smc_thrash_rewritten(&block->smc_strikes, &pages[block->phys >> 12].smc_strikes,
                         codeblock_ran(block->runs_left, SMC_THRASH_MIN_RUNS), tsc, &cooldown);
    if (block == block_checked_entering) {
        block_checked_cooldown_block = block;
        block_checked_cooldown       = cooldown;
    } else
        block->smc_cooldown_until = cooldown;
    /*Off its page lists, as invalidate_block() takes a block off them: the
      put back or the compile lists it again*/
    remove_from_block_list(block, block->pc);
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    link_slots_clear(block);
#    endif
    block_version_keep_current(block);
    /*Without code its next entry has to reach codegen_block_check(): the
      dirty bits the flush clears would let the loop run it*/
    block->dirty_mask = &block_untracked_mask;
    block_chk_dirty_sync(block);
    if (block->dirty_mask2)
        block->dirty_mask2 = &block_untracked_mask;
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#    endif
}

/*A checked block left to be compiled again, not compiled yet: its page
  flushed first if its bytes were written again, as the loop flushes the page
  of a block on the dirty list, then the kept version compiled from the bytes
  there now put back, as codegen_block_restore_version() puts one back.
  Returns 1 if one was.*/
int
codegen_block_put_back(codeblock_t *block)
{
    if (block_checked_written(block))
        codegen_check_flush(&pages[block->phys >> 12], pages[block->phys >> 12].dirty_mask, block->phys);
    return block_version_put_back(block);
}

/*A checked block about to run. Unless its current code is marked by writes,
  the bytes its current version was compiled from are compared with guest
  memory, through the mapping they were compiled under, as its code does on a
  linked entry; code marked by writes is told by its dirty bits only. If they
  are the same and none was written since, it runs.
  Otherwise it is handled as the loop handles a block marked by writes and
  written, or left without code since: in its SMC cooldown it is interpreted
  as it is (returns -1); otherwise its page is flushed, which invalidates it
  (block_checked_invalidate()), and it gets the kept version compiled from the
  bytes there now (returns 1: it runs as the loop runs a block it put back,
  without the tests before) or is compiled again where it is.*/
int
codegen_block_check(codeblock_t *block)
{
    const uint16_t cur     = block_version_cur[get_block_nr(block)];
    const uint8_t *exec    = _mem_exec[block->phys >> MEM_GRANULARITY_BITS];
    const int      written = block_checked_written(block);
    int            same    = 0;

    /*Current code marked by writes (a version compiled before the block was
      checked): only its dirty bits tell, as for any block marked by writes*/
    if (block->dirty_mask != &block_untracked_mask)
        same = 1;
    else if (cur && (exec == block_versions[cur].exec)) {
        const block_version_t *v    = &block_versions[cur];
        const uint8_t         *code = &exec[(block->phys & MEM_GRANULARITY_MASK) & ~0x3f];
        int                    c;

        for (c = 0; c < v->chk_nr; c++) {
            uint64_t word;

            memcpy(&word, &code[v->chk_word[c] * 8], sizeof(word));
            if ((word ^ v->chk[c * 2]) & v->chk[c * 2 + 1])
                break;
        }
        same = (c == v->chk_nr);
    }
    if (same && !written)
        return 0;
    if (block->smc_cooldown_until > tsc)
        return -1;

    block_checked_entering = block;
    if (written)
        codegen_check_flush(&pages[block->phys >> 12], pages[block->phys >> 12].dirty_mask, block->phys);
    /*Not written, but not the bytes it was compiled from (another mapping)*/
    if (block->head_mem_block)
        block_checked_invalidate(block);
    block_checked_entering = NULL;
    if (block_version_put_back(block))
        return 1;
    block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#    endif
    return 0;
}
#endif

/*Called in the compile pass after an instruction at linear addr is compiled
  and before it runs. The bytes the compiler read must be the start snapshot,
  fetched through the memory it was taken from; else the version is not
  recorded. A write that leaves no dirty bits and is undone before the end of
  the compile is only seen here.*/
void
codegen_block_version_check_ins(uint32_t addr)
{
    const codeblock_t *block = &codeblock[block_current];
    const uint8_t     *fetch;
    uint32_t           offset;
    uint32_t           end;

    if (!block_version_start_ok)
        return;

    offset = addr - (block->pc & ~0x3f);
    if (((addr >> 12) != pccache) || (offset >= 128)) {
        block_version_start_ok = 0;
        return;
    }
    fetch = (const uint8_t *) ((((uintptr_t) &pccache2[addr]) & 0x00000000ffffffffULL) | (((uintptr_t) &pccache2[0]) & 0xffffffff00000000ULL));
    if (fetch != &block_version_start_exec[((block->phys & MEM_GRANULARITY_MASK) & ~0x3f) + offset]) {
        block_version_start_ok = 0;
        return;
    }
    /*To the end of the snapshot: the decoders take any number of prefixes,
      so an instruction is not bounded at 15 bytes here*/
    if (memcmp(fetch, &block_version_start_code[offset], 128 - offset)) {
        block_version_start_ok = 0;
        return;
    }
    /*And it must end inside the snapshot: after its prefixes come at most 0F,
      a second opcode byte, ModR/M, SIB, disp32 and imm32*/
    for (end = offset; end < 128; end++) {
        switch (block_version_start_code[end]) {
            case 0x26: case 0x2e: case 0x36: case 0x3e: case 0x64: case 0x65:
            case 0x66: case 0x67: case 0xf0: case 0xf2: case 0xf3:
                continue;
            default:
                break;
        }
        break;
    }
    if ((end + 12) > 128)
        block_version_start_ok = 0;
}

void
codegen_flush(void)
{
    /*A TLB flush ends get_phys()'s cached translation, as it ends the fetch
      cache: the next block lookup translates again, and its walk sets the
      Accessed bits the fetch would. A real entry is page aligned, so this
      tag never hits, there or in the link routines' compare.*/
    get_phys_phys = 0xffffffff;
}

int      codegen_page2_walked;
int      codegen_fetch_walk_off;
int      codegen_fetch_walk_defer;
uint32_t codegen_fetch_walk_deferred;

/*Compiled code fetches nothing: the walk of the second page the
  interpreter's fetch would make at this point, every time the block gets
  here. A full barrier, so it is safe inside a handler (the temporaries are
  written back), and its exit takes the #PF from this instruction's oldpc.
  The cycles it may leave with are stamped on it (uop_alloc)*/
void
codegen_fetch_walk(uint32_t addr)
{
    ir_data_t *ir = codegen_get_ir_data();

    codegen_page2_walked = 1;
    /*A displacement whose walk is made later (codegen_skip, codegen.c's
      codegen_generate_ea), linear 0 as any other address*/
    if (codegen_fetch_walk_defer) {
        codegen_fetch_walk_deferred = addr | 1;
        return;
    }
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_CALL_INSTRUCTION_FUNC(ir, dynarec_fetch_walk, addr);
}

/*The block being compiled has its first code in its second chunk or page:
  track writes to that page from now on (recomp_page2). Its direct write
  lookups are dropped, as codegen_block_start_recompile() does for the
  first page, so that the next write there adds a tracked one.*/
static void
codegen_block_track_page2(codeblock_t *block)
{
    const uint32_t pc_2   = codeblock_pc_2(block);
    const uint32_t phys_2 = get_phys_noabrt(pc_2);

    if ((phys_2 == 0xffffffff) || ((phys_2 & ~0xfff) == recomp_page))
        return;
    recomp_page2 = phys_2 & ~0xfff;
    if (!pages[phys_2 >> 12].block && !pages[phys_2 >> 12].block_2)
        mem_flush_write_page(phys_2, pc_2);
}

void
codegen_mark_code_present_multibyte(codeblock_t *block, uint32_t start_pc, int len)
{
    const uint64_t had_page_2 = block->page_mask2;

    if (len) {
        uint32_t end_pc = start_pc + (len - 1);

        if (block->flags & CODEBLOCK_BYTE_MASK) {
            uint32_t start_pc_masked = start_pc & PAGE_MASK_MASK;
            uint32_t end_pc_masked   = end_pc & PAGE_MASK_MASK;

            if ((start_pc ^ block->pc) & ~0x3f) /*Starts in second page*/
            {
                for (; start_pc_masked <= end_pc_masked; start_pc_masked++)
                    block->page_mask2 |= ((uint64_t) 1 << start_pc_masked);
            } else if (((start_pc + (len - 1)) ^ block->pc) & ~0x3f) /*Crosses both pages*/
            {
                for (; start_pc_masked <= 63; start_pc_masked++)
                    block->page_mask |= ((uint64_t) 1 << start_pc_masked);
                for (start_pc_masked = 0; start_pc_masked <= end_pc_masked; start_pc_masked++)
                    block->page_mask2 |= ((uint64_t) 1 << start_pc_masked);
            } else /*First page only*/
            {
                for (; start_pc_masked <= end_pc_masked; start_pc_masked++)
                    block->page_mask |= ((uint64_t) 1 << start_pc_masked);
            }
        } else {
            uint32_t start_pc_shifted = start_pc >> PAGE_MASK_SHIFT;
            uint32_t end_pc_shifted   = end_pc >> PAGE_MASK_SHIFT;
            start_pc_shifted &= PAGE_MASK_MASK;
            end_pc_shifted &= PAGE_MASK_MASK;

            if ((start_pc ^ block->pc) & ~0xfff) /*Starts in second page*/
            {
                for (; start_pc_shifted <= end_pc_shifted; start_pc_shifted++)
                    block->page_mask2 |= ((uint64_t) 1 << start_pc_shifted);
            } else if (((start_pc + (len - 1)) ^ block->pc) & ~0xfff) /*Crosses both pages*/
            {
                for (; start_pc_shifted <= 63; start_pc_shifted++)
                    block->page_mask |= ((uint64_t) 1 << start_pc_shifted);
                for (start_pc_shifted = 0; start_pc_shifted <= end_pc_shifted; start_pc_shifted++)
                    block->page_mask2 |= ((uint64_t) 1 << start_pc_shifted);
            } else /*First page only*/
            {
                for (; start_pc_shifted <= end_pc_shifted; start_pc_shifted++)
                    block->page_mask |= ((uint64_t) 1 << start_pc_shifted);
            }
        }
    }
    if (!had_page_2 && block->page_mask2)
        codegen_block_track_page2(block);
}
