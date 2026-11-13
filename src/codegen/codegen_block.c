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
#include "codegen_reg.h"
#include "codegen_smc_policy.h"

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
    uint64_t            family_ops[2]; /*codegen_family_ops when compiled*/
    uint32_t            phys_2;
    uint32_t            owner; /*Number of the code block*/
    uint16_t            cs_sel; /*CS selector: x87 code stores it as the FPU CS*/
    uint16_t            status;
    uint16_t            flags; /*CODEBLOCK_HAS_FPU, CODEBLOCK_STATIC_TOP and CODEBLOCK_OPCODE_FAMILY of this version*/
    uint16_t            next;  /*Next older kept version of the owner, or the next free one*/
    uint8_t             TOP;
    uint8_t             state;
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

uint64_t codegen_family_ops[2];

/*Per block: smc_family_hold() state*/
static uint8_t block_family_hold[BLOCK_SIZE];

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

    /*Only a compiled block the hash already points to.*/
    if ((block->flags & (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_IN_DIRTY_LIST)) != CODEBLOCK_WAS_RECOMPILED)
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

            codegen_link_mcache[idx].r = *slot;
            block->link_mcache       = idx + 1;
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
        /*Free list is empty, check the dirty lists, the plain one first*/
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

/*Blocks on the first list of a page are kept in 64 lists, one per 64-byte
  chunk of the page, so codegen_check_flush() walks only those that may cover
  a dirty chunk: a page may hold hundreds of blocks while a write dirties one
  or two chunks. page_t::block is the number of the page's entry here, 0 if
  the page has no blocks. Not kept in page_t itself: pages has an entry for
  every 4K of the address space.*/
typedef struct page_blocks_t {
    uint64_t  used;       /*Bit per chunk list with blocks on it*/
    uint64_t  byte_used;  /*Bit per chunk list with byte mask blocks on it*/
    uint64_t *byte_dirty; /*page->byte_dirty_mask when the entry was made*/
    uint32_t  count;
    uint32_t  next_free;
    /*A block's dirty mask is not where the page's masks are now: the chunk
      lists can not tell which blocks a write hit, so all are checked.*/
    uint8_t mixed;
    /*Most chunks past its own a block on each list covers*/
    uint8_t  span[64];
    uint32_t head[64];
} page_blocks_t;

static page_blocks_t *page_blocks;
static uint32_t       page_blocks_size; /*Entry 0 is not used*/
static uint32_t       page_blocks_top;
static uint32_t       page_blocks_free;

static uint32_t
page_blocks_alloc(page_t *page)
{
    uint32_t       nr = page_blocks_free;
    page_blocks_t *pb;

    if (nr) {
        /*Freed when its last block came off, so its lists are empty*/
        page_blocks_free = page_blocks[nr].next_free;
        pb               = &page_blocks[nr];
        pb->mixed        = 0;
        pb->byte_dirty   = page->byte_dirty_mask;
        return nr;
    }
    if ((page_blocks_top + 1) >= page_blocks_size) {
        uint32_t       size = page_blocks_size ? (page_blocks_size * 2) : 256;
        page_blocks_t *p    = realloc(page_blocks, size * sizeof(page_blocks_t));

        if (!p)
            fatal("page_blocks_alloc: out of memory\n");
        page_blocks      = p;
        page_blocks_size = size;
    }
    nr = ++page_blocks_top;
    pb = &page_blocks[nr];
    memset(pb, 0, sizeof(page_blocks_t));
    pb->byte_dirty = page->byte_dirty_mask;
    return nr;
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
    family_review_reset();
    mem_reset_page_blocks();
    page_blocks_top  = 0;
    page_blocks_free = 0;

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

static void
add_to_block_list(codeblock_t *block)
{
    page_t        *page     = &pages[block->phys >> 12];
    uint32_t       block_nr = get_block_nr(block);
    uint32_t       block_prev_nr;
    page_blocks_t *pb;
    int            chunk;
    int            span;

#ifndef RELEASE_BUILD
    if (!block->page_mask)
        fatal("add_to_block_list - mask = 0 %" PRIx64 " %" PRIx64 "\n", block->page_mask, block->page_mask2);
#endif

    if (!page->block)
        page->block = page_blocks_alloc(page);
    pb = &page_blocks[page->block];

    /*The list of the chunk whose dirty bits the block checks, the first one
      it covers*/
    if (block->flags & CODEBLOCK_BYTE_MASK) {
        chunk = (block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;
        span  = 0;
        pb->byte_used |= (uint64_t) 1 << chunk;
        if ((block->dirty_mask != &page->byte_dirty_mask[chunk]) || (page->byte_dirty_mask != pb->byte_dirty))
            pb->mixed = 1;
    } else {
        if (block->page_mask) {
            chunk = __builtin_ctzll(block->page_mask);
            span  = 63 - __builtin_clzll(block->page_mask) - chunk;
        } else {
            chunk = 0;
            span  = 63;
        }
        if (block->dirty_mask != &page->dirty_mask)
            pb->mixed = 1;
    }

    block_prev_nr     = pb->head[chunk];
    block->page_chunk = chunk;
    block->prev       = BLOCK_INVALID;
    block->next       = block_prev_nr;
    if (block_prev_nr)
        codeblock[block_prev_nr].prev = block_nr;
    pb->head[chunk] = block_nr;
    pb->used |= (uint64_t) 1 << chunk;
    if (span > pb->span[chunk])
        pb->span[chunk] = span;
    pb->count++;
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
    {
        page_t        *page  = &pages[block->phys >> 12];
        page_blocks_t *pb    = &page_blocks[page->block];
        int            chunk = block->page_chunk;

#ifndef RELEASE_BUILD
        if (!page->block || (!block->prev && (pb->head[chunk] != get_block_nr(block))))
            fatal("remove_from_block_list: not on its page's list\n");
#endif
        if (block->prev)
            codeblock[block->prev].next = block->next;
        else {
            pb->head[chunk] = block->next;
            if (!block->next) {
                pb->used &= ~((uint64_t) 1 << chunk);
                pb->byte_used &= ~((uint64_t) 1 << chunk);
                pb->span[chunk] = 0;
            }
        }
        if (block->next)
            codeblock[block->next].prev = block->prev;
        if (!--pb->count) {
            pb->next_free    = page_blocks_free;
            page_blocks_free = page->block;
            page->block      = 0;
            mem_flush_write_page(block->phys, 0);
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

    if (block->prev_2) {
        codeblock[block->prev_2].next_2 = block->next_2;
        if (block->next_2)
            codeblock[block->next_2].prev_2 = block->prev_2;
    } else {
        pages[block->phys_2 >> 12].block_2 = block->next_2;
        if (block->next_2)
            codeblock[block->next_2].prev_2 = BLOCK_INVALID;
        else
            mem_flush_write_page(block->phys_2, 0);
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

/*Blocks that read their opcodes as they run. Only a rewrite decides whether a
  block keeps doing so, and opcode writes no longer rewrite it: code whose
  opcodes stopped changing would stay in that slower form for good. Checked
  after each CPU run (codegen_family_review()).*/
#define FAMILY_REVIEW_MAX    256
#define FAMILY_REVIEW_STABLE 2

static uint32_t family_review_nr[FAMILY_REVIEW_MAX];
static uint8_t  family_review_stable[FAMILY_REVIEW_MAX];
static uint64_t family_review_listed[BLOCK_SIZE / 64];

static void
family_review_add(uint32_t block_nr)
{
    uint64_t bit = 1ULL << (block_nr & 63);

    if (family_review_listed[block_nr >> 6] & bit) {
        for (int c = 0; c < family_review_count; c++) {
            if (family_review_nr[c] == block_nr) {
                family_review_stable[c] = 0;
                break;
            }
        }
        return;
    }
    if (family_review_count >= FAMILY_REVIEW_MAX)
        return;
    family_review_listed[block_nr >> 6] |= bit;
    family_review_nr[family_review_count]     = block_nr;
    family_review_stable[family_review_count] = 0;
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

        if (!block->valid || !cur || (v->head_mem_block != block->head_mem_block) ||
            ((block->flags & (CODEBLOCK_OPCODE_FAMILY | CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_IN_DIRTY_LIST)) != (CODEBLOCK_OPCODE_FAMILY | CODEBLOCK_WAS_RECOMPILED)))
            drop = 1;
        else if (block_family_ops_changed(block))
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
            family_review_nr[c]     = family_review_nr[family_review_count];
            family_review_stable[c] = family_review_stable[family_review_count];
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

        smc_thrash_rewritten(&block->smc_strikes, &page->smc_strikes,
                             codeblock_ran(block->runs_left, SMC_THRASH_MIN_RUNS),
                             tsc, &block->smc_cooldown_until);
        {
            uint8_t *hold     = &block_family_hold[get_block_nr(block)];
            int      ran_long = codeblock_ran(block->runs_left, SMC_FAMILY_MIN_RUNS);
            int      keep     = 0;

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
    remove_from_block_list(block, old_pc);
    if (block->head_mem_block) {
        uint32_t block_nr = get_block_nr(block);
        uint16_t cur      = block_version_cur[block_nr];

        if (cur && (block_versions[cur].head_mem_block == block->head_mem_block)) {
            /*Keep it, newest first, and no more than BLOCK_VERSION_PER_BLOCK*/
            uint16_t nr;
            int      c = 1;

            block_versions[cur].state    = BLOCK_VERSION_KEPT;
            block_version_kept_nr++;
            block_versions[cur].next     = block_version_kept[block_nr];
            block_version_kept[block_nr] = cur;
            for (nr = cur; block_versions[nr].next; nr = block_versions[nr].next) {
                if (++c > BLOCK_VERSION_PER_BLOCK) {
                    uint16_t oldest          = block_versions[nr].next;
                    block_versions[nr].next  = 0;
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

void
codegen_delete_random_block(int required_mem_block)
{
    static int evict_probe = 0;
    int block_nr = evict_probe & BLOCK_MASK;
    int steps    = 0;

    /* A second chance, as a clock: a block run since the sweep last came by
       is passed over once; one full turn clears them all, so the sweep
       ends. Evicting blindly threw out code about to run again. */
    while (1) {
        if (block_nr && block_nr != block_current) {
            codeblock_t *block = &codeblock[block_nr];

            if (block->valid && block->used) {
                block->used = 0;
                block_nr    = (block_nr + 1) & BLOCK_MASK;
                continue;
            }
            if (block->valid && (!required_mem_block || block->head_mem_block)) {
                evict_probe = (block_nr + 1) & BLOCK_MASK;
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

void
codegen_check_flush(page_t *page, UNUSED(uint64_t mask), UNUSED(uint32_t phys_addr))
{
    uint32_t block_nr               = page->block;
    int      remove_from_evict_list = 0;
    /* The byte mask words cleared below: those of the chunks dirty_mask has,
       as every write marks both, and those the page's byte mask blocks
       check, as a write through another page sharing the byte masks or past
       the end of the page before marks only the word. */
    uint64_t words                  = 0;

    if (block_nr) {
        page_blocks_t *pb     = &page_blocks[block_nr];
        uint64_t       used   = pb->used;
        int            all    = pb->mixed || (page->mem == page_ff) || (page->byte_dirty_mask != pb->byte_dirty);

        words = all ? UINT64_MAX : pb->byte_used;

        /* A list's byte mask blocks check the byte mask word of its chunk,
           the rest page->dirty_mask over the chunks the list covers. The
           byte masks are read here, not taken from page->dirty_mask: pages
           remapped onto the same RAM share them, but not dirty_mask.
           Blocks it invalidates come off the lists, and an entry left with
           none is freed, but it is not reused until a block is added. */
        while (used) {
            int      c     = __builtin_ctzll(used);
            uint64_t cover = (((uint64_t) 2 << pb->span[c]) - 1) << c;

            used &= used - 1;
            if (!all && !(page->dirty_mask & cover) &&
                !((pb->byte_used & ((uint64_t) 1 << c)) && page->byte_dirty_mask[c]))
                continue;

            block_nr = pb->head[c];
            while (block_nr) {
                codeblock_t *block      = &codeblock[block_nr];
                uint32_t     next_block = block->next;

                if (*block->dirty_mask & block->page_mask) {
                    invalidate_block(block);
                }
#ifndef RELEASE_BUILD
                if (block_nr == next_block)
                    fatal("Broken 1\n");
#endif
                block_nr = next_block;
            }
        }
    }

    block_nr = page->block_2;

    while (block_nr) {
        codeblock_t *block      = &codeblock[block_nr];
        uint32_t     next_block = block->next_2;

        if (block->flags & CODEBLOCK_BYTE_MASK) {
            ptrdiff_t c = block->dirty_mask2 - page->byte_dirty_mask;

            words |= ((c >= 0) && (c < 64)) ? ((uint64_t) 1 << c) : UINT64_MAX;
        }
        if (*block->dirty_mask2 & block->page_mask2) {
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
    words |= page->dirty_mask;
    page->dirty_mask = 0;

    /* A page with no RAM behind it has no byte masks of its own */
    if (page->mem != page_ff) {
        /* Locals: the stores below could otherwise alias the page's pointers,
           reloading them for every word. */
        uint64_t *restrict code_present = page->byte_code_present_mask;
        uint64_t *restrict dirty        = page->byte_dirty_mask;

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

    block->valid             = 1;
    block->ins               = 0;
    block->smc_strikes        = 0;
    block->smc_family_score   = 0;
    block_family_hold[block_current] = 0;
    /* On a page whose blocks keep being rewritten before they pay for
       compiling, a new one is likely to be too: interpret it for a while
       first. One that pays for itself later takes a strike off the page. */
    block->smc_cooldown_until = smc_thrash_new_block_cooldown(page->smc_strikes, tsc);
    block->pc                = cs + cpu_state.pc;
    block->_cs         = cs;
    block->phys        = phys_addr;
    block->dirty_mask  = &page->dirty_mask;
    block->dirty_mask2 = NULL;
    block->next = block->prev = BLOCK_INVALID;
    block->next_2 = block->prev_2 = BLOCK_INVALID;
    block->page_mask = block->page_mask2 = 0;
    block->flags                         = CODEBLOCK_STATIC_TOP;
    block->status                        = cpu_cur_status;
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#endif

    recomp_page = block->phys & ~0xfff;
    codeblock_tree_add(block);
}

static ir_data_t *ir_data;

ir_data_t *
codegen_get_ir_data(void)
{
    return ir_data;
}

void
codegen_block_start_recompile(codeblock_t *block)
{
    page_t *page = &pages[block->phys >> 12];

    if (!page->block)
        mem_flush_write_page(block->phys, cs + cpu_state.pc);

    block_num     = HASH(block->phys);
    block_current = get_block_nr(block); // block->pnt;

#ifndef RELEASE_BUILD
    if (block->pc != cs + cpu_state.pc || (block->flags & CODEBLOCK_WAS_RECOMPILED))
        fatal("Recompile to used block!\n");
#endif

    if (block_version_cur[block_current]) {
        block_version_free(block_version_cur[block_current]);
        block_version_cur[block_current] = 0;
    }
    /*Only blocks whose second chunk is in the same page get versions: writes
      to the next page during the compile are not tracked (recomp_page covers
      this page only), and a mapping change there can drop page_mask2.*/
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
    block->ins                           = 0;
    codegen_family_ops[0] = codegen_family_ops[1] = 0;

    cpu_block_end = 0;

    codegen_block_cycles = 0;
    codegen_timing_block_start();

    recomp_page = block->phys & ~0xfff;

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
}

void
codegen_block_remove(void)
{
    codeblock_t *block = &codeblock[block_current];

    delete_block(block);

    recomp_page = -1;
}

void
codegen_block_generate_end_mask_recompile(void)
{
    codeblock_t *block = &codeblock[block_current];
    page_t      *p;

    p = &pages[block->phys >> 12];
    if (block->flags & CODEBLOCK_BYTE_MASK) {
        int offset = (block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

        p->byte_code_present_mask[offset] |= block->page_mask;
    } else
        p->code_present_mask |= block->page_mask;

    if ((*(block->dirty_mask) & block->page_mask) && !page_in_evict_list(p))
        page_add_to_evict_list(p);

    block->phys_2 = -1;
    block->next_2 = block->prev_2 = BLOCK_INVALID;
    if (block->page_mask2) {
        /*The second chunk or page is the one the lookup checks
          (386_dynarec.c): the next 64 bytes of a byte-mask block, else the
          next page. Not codegen_endpc, which guesses where the last
          instruction ends from where it starts: one of 11 bytes or more
          marks bytes past the guess, in a chunk or page it doesn't name.*/
        uint32_t pc_2 = block->pc + ((block->flags & CODEBLOCK_BYTE_MASK) ? 0x40 : 0x400);

        block->phys_2 = get_phys_noabrt(pc_2);
        if (block->phys_2 != -1) {
            page_t *page_2 = &pages[block->phys_2 >> 12];

            if (block->flags & CODEBLOCK_BYTE_MASK) {
                int offset = (block->phys_2 >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

                page_2->byte_code_present_mask[offset] |= block->page_mask2;
                block->dirty_mask2 = &page_2->byte_dirty_mask[offset];
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

    recomp_page = -1;
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
    p->code_present_mask |= block->page_mask;
    if ((p->dirty_mask & block->page_mask) && !page_in_evict_list(p))
        page_add_to_evict_list(p);

    block->phys_2     = -1;
    block->page_mask2 = 0;
    block->next_2 = block->prev_2 = BLOCK_INVALID;
    if ((block->pc ^ codegen_endpc) & ~0xfff) {
        block->phys_2 = get_phys_noabrt(codegen_endpc);
        if (block->phys_2 != -1) {
            page_t *page_2 = &pages[block->phys_2 >> 12];

            start_pc = 0;
            end_pc   = (codegen_endpc & 0xfff) >> PAGE_MASK_SHIFT;
            for (; start_pc <= end_pc; start_pc++)
                block->page_mask2 |= ((uint64_t) 1 << start_pc);

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

    recomp_page = -1;
}

void
codegen_block_end(void)
{
    codeblock_t *block = &codeblock[block_current];

    codegen_block_generate_end_mask_mark();
    add_to_block_list(block);
}

/*Takes the kept version of the block matching its bytes and the CPU state,
  and with none of the flags in skip, out of the kept list; 0 if there is none*/
static uint16_t
block_version_take(const codeblock_t *block, uint16_t skip)
{
    uint32_t  block_nr = get_block_nr(block);
    uint16_t *link     = &block_version_kept[block_nr];

    while (*link) {
        uint16_t         nr = *link;
        block_version_t *v  = &block_versions[nr];

        if ((v->flags & skip) || (v->cs_sel != CS) || ((v->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) ||
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
    uint16_t     nr    = block_version_take(block, CODEBLOCK_OPCODE_FAMILY);

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

    codegen_accumulate_record(ACC_END, 0, 0, 0, 0, 0);
    codegen_timing_block_end();
    codegen_accumulate_timing(ir_data);

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

    /*The unrolled branch falls through when taken. Only its not-taken
      exit owes the drain and the branch credit.*/
    if (!codegen_ir_unrolling())
        codegen_accumulate_flush(ir_data);
    codegen_ir_compile(ir_data, block);
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
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
    uint16_t nr          = block_version_take(block, skip_family ? CODEBLOCK_OPCODE_FAMILY : 0);

    if (!nr)
        return 0;
    block_version_apply(block, nr);
    return 1;
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

void
codegen_mark_code_present_multibyte(codeblock_t *block, uint32_t start_pc, int len)
{
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
}
