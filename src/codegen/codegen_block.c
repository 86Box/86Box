#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

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

uint8_t *block_write_data = NULL;

int      codegen_flat_ds;
int      codegen_flat_ss;
int      mmx_ebx_ecx_loaded;
int      codegen_flags_changed = 0;
int      codegen_fpu_entered   = 0;
int      codegen_mmx_entered   = 0;
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
static int codegen_block_ins;
static int codegen_block_full_ins;

static uint32_t last_op32;
static x86seg  *last_ea_seg;
static int      last_ssegs;

#ifdef DEBUG_EXTRA
uint32_t instr_counts[256 * 256];
#endif

static uint32_t block_free_list;
static void     delete_block(codeblock_t *block);
static void     delete_dirty_block(codeblock_t *block);

/*Temporary list of code blocks that have recently been evicted. This allows for
  some historical state to be kept when a block is the target of self-modifying
  code.

  The size of this list is limited to DIRTY_LIST_MAX_SIZE blocks. When this is
  exceeded the oldest entry will be moved to the free list.*/
static uint32_t block_dirty_list_head;
static uint32_t block_dirty_list_tail;
static int      dirty_list_size = 0;
#define DIRTY_LIST_MAX_SIZE 64

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
    uint32_t            phys_2;
    uint32_t            owner; /*Number of the code block*/
    uint16_t            cs_sel; /*CS selector: x87 code stores it as the FPU CS*/
    uint16_t            status;
    uint16_t            flags; /*CODEBLOCK_HAS_FPU and CODEBLOCK_STATIC_TOP of this version*/
    uint16_t            next;  /*Next older kept version of the owner, or the next free one*/
    uint8_t             TOP;
    uint8_t             state;
    uint8_t             code[128]; /*The first 64-byte chunk, then the second*/
} block_version_t;

/*Index 0 is not used: 0 means none*/
static block_version_t block_versions[BLOCK_VERSION_NR + 1];
static uint16_t        block_version_free_list;
static uint16_t        block_version_hand = 1;
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
}

static void
block_version_free(uint16_t nr)
{
    block_version_t *v = &block_versions[nr];

    if ((v->state == BLOCK_VERSION_KEPT) && v->head_mem_block)
        codegen_allocator_free_version(v->head_mem_block);
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
    for (int c = 0; c < 64; c++) {
        if ((mask & ((uint64_t) 1 << c)) && (fetch[c] != code[c]))
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

void
codegen_link_reset(void)
{
    memset(codegen_link_slots, 0, sizeof(codegen_link_slots));
}

void
codegen_link_block_changed(codeblock_t *block)
{
    if (!codegen_block_link)
        return;

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
#ifndef RELEASE_BUILD
    if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
        fatal("block_dirty_list_add: block=%p already in dirty list\n", block);
#endif
    if (block_dirty_list_head != BLOCK_INVALID) {
        codeblock_t *old_head = &codeblock[block_dirty_list_head];

        block->next           = block_dirty_list_head;
        block->prev           = BLOCK_INVALID;
        block_dirty_list_head = old_head->prev = get_block_nr(block);
    } else {
        /*List empty*/
        block->prev = block->next = BLOCK_INVALID;
        block_dirty_list_head = block_dirty_list_tail = get_block_nr(block);
    }
    block->flags |= CODEBLOCK_IN_DIRTY_LIST;
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#endif
    dirty_list_size++;
    if (dirty_list_size > DIRTY_LIST_MAX_SIZE) {
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

        block_dirty_list_tail             = evict_block->prev;
        codeblock[evict_block->prev].next = BLOCK_INVALID;

        dirty_list_size--;
        evict_block->flags &= ~CODEBLOCK_IN_DIRTY_LIST;
        delete_dirty_block(evict_block);
    }
}

static void
block_dirty_list_remove(codeblock_t *block)
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

static codeblock_t *
block_free_list_get(void)
{
    codeblock_t *block = NULL;

    while (!block_free_list) {
        /*Free list is empty, check the dirty list*/
        if (block_dirty_list_tail) {
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

    block           = &codeblock[block_free_list];
    block_free_list = block->next;
    block->flags &= ~CODEBLOCK_IN_FREE_LIST;
    block->next = 0;
    return block;
}

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
    block_free_list = 0;
    for (uint32_t c = 0; c < BLOCK_SIZE; c++)
        block_free_list_add(&codeblock[c]);
    block_dirty_list_head = block_dirty_list_tail = 0;
    dirty_list_size                               = 0;
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
            delete_block(block);
        }
    }

    memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
    memset(codeblock_hash, 0, HASH_SIZE * sizeof(uint32_t));
    mem_reset_page_blocks();

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
    if (!block->page_mask)
        return;
#ifndef RELEASE_BUILD
    if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
        fatal("remove_from_block_list: in dirty list\n");
#endif
    if (block->prev) {
        codeblock[block->prev].next = block->next;
        if (block->next)
            codeblock[block->next].prev = block->prev;
    } else {
        pages[block->phys >> 12].block = block->next;
        if (block->next)
            codeblock[block->next].prev = BLOCK_INVALID;
        else
            mem_flush_write_page(block->phys, 0);
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
    remove_from_block_list(block, old_pc);
    block_dirty_list_add(block);
    if (block->head_mem_block) {
        uint32_t block_nr = get_block_nr(block);
        uint16_t cur      = block_version_cur[block_nr];

        if (cur && (block_versions[cur].head_mem_block == block->head_mem_block)) {
            /*Keep it, newest first, and no more than BLOCK_VERSION_PER_BLOCK*/
            uint16_t nr;
            int      c = 1;

            block_versions[cur].state    = BLOCK_VERSION_KEPT;
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

    block_nr = page->block_2;

    while (block_nr) {
        codeblock_t *block      = &codeblock[block_nr];
        uint32_t     next_block = block->next_2;

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
    page->dirty_mask = 0;

    for (uint8_t c = 0; c < 64; c++) {
        if (page->byte_code_present_mask[c] & page->byte_dirty_mask[c])
            remove_from_evict_list = 0;
        page->byte_code_present_mask[c] &= ~page->byte_dirty_mask[c];
        page->byte_dirty_mask[c] = 0;
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

    block->valid       = 1;
    block->ins         = 0;
    block->pc          = cs + cpu_state.pc;
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

    block->page_mask = block->page_mask2 = 0;
    block->ins                           = 0;

    cpu_block_end = 0;

    last_op32   = -1;
    last_ea_seg = NULL;
    last_ssegs  = -1;

    codegen_block_cycles = 0;
    codegen_timing_block_start();

    codegen_block_ins      = 0;
    codegen_block_full_ins = 0;

    recomp_page = block->phys & ~0xfff;

    codegen_flags_changed = 0;
    codegen_flags_known_reset();
    codegen_fpu_entered   = 0;
    codegen_mmx_entered   = 0;
    codegen_fpu_class_dirty = 0;

    codegen_fpu_loaded_iq[0] = codegen_fpu_loaded_iq[1] = codegen_fpu_loaded_iq[2] = codegen_fpu_loaded_iq[3] = codegen_fpu_loaded_iq[4] = codegen_fpu_loaded_iq[5] = codegen_fpu_loaded_iq[6] = codegen_fpu_loaded_iq[7] = 0;

    cpu_state.seg_ds.checked = cpu_state.seg_es.checked = cpu_state.seg_fs.checked = cpu_state.seg_gs.checked = (cr0 & 1) ? 0 : 1;

    block->TOP = cpu_state.TOP & 7;
    block->flags |= CODEBLOCK_WAS_RECOMPILED;

    codegen_flat_ds = !(block->status & CPU_STATUS_NOTFLATDS);
    codegen_flat_ss = !(block->status & CPU_STATUS_NOTFLATSS);
    codegen_flat_es = !(block->status & CPU_STATUS_NOTFLATES);

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

void
codegen_block_end_recompile(codeblock_t *block)
{
    uint64_t page_mask2_compiled = block->page_mask2;

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
            v->exec_2 = block->page_mask2 ? block_version_start_exec_2 : NULL;
            memcpy(v->code, block_version_start_code, sizeof(v->code));
            if (block_version_chunk_matches(block->phys, v->exec, block->page_mask, v->code) &&
                (!block->page_mask2 ||
                 ((block->phys_2 == block_version_start_phys_2) &&
                  block_version_chunk_matches(block->phys_2, v->exec_2, block->page_mask2, &v->code[64])))) {
                v->cs_sel                             = block_version_start_cs;
                v->head_mem_block                     = block->head_mem_block;
                v->page_mask                          = block->page_mask;
                v->page_mask2                         = block->page_mask2;
                v->phys_2                             = block->phys_2;
                v->owner                              = get_block_nr(block);
                v->status                             = block->status;
                v->flags                              = block->flags & (CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP);
                v->TOP                                = block->TOP;
                v->state                              = BLOCK_VERSION_CURRENT;
                block_version_cur[get_block_nr(block)] = nr;
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

        /*Match: it becomes the block's current code, as a compile would*/
        *link                       = v->next;
        v->next                     = 0;
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
        block->flags          = (block->flags & ~(CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP)) | v->flags | CODEBLOCK_WAS_RECOMPILED;
        codegen_block_generate_end_mask_recompile();
        add_to_block_list(block);
        block_version_owner_sync(block_nr);
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
        codegen_link_block_changed(block);
#endif
        return 1;
    }
    return 0;
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
    return;
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
