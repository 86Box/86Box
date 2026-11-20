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
#include "codegen_public.h"
#include "codegen_accumulate.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_link.h"
#include "codegen_public.h"
#include "codegen_reg.h"

extern int codegen_record_faulted; /* 386_common.c */

uint8_t *block_write_data = NULL;

int      codegen_flat_ds;
int      codegen_flat_ss;
int      codegen_wide_ds;
int      codegen_wide_ss;
codeblock_t *codegen_family_nofix_block;
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
/*1 + the offset in the second chunk of the last byte of an immediate read
  there at run time (immediate_off_first_page()), 0 if none*/
int codegen_imm_page2;

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

/*Temporary list of code blocks that have recently been evicted. This allows for
  some historical state to be kept when a block is the target of self-modifying
  code.

  The size of this list is limited to DIRTY_LIST_MAX_SIZE blocks. When this is
  exceeded the oldest entry will be moved to the free list.*/
static uint32_t block_dirty_list_head;
static uint32_t block_dirty_list_tail;
static int      dirty_list_size = 0;
#define DIRTY_LIST_MAX_SIZE 64

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
    /*Family opcodes of a CODEBLOCK_ALU_FAMILY version, in each chunk: not in
      its page masks, but compared as its code is, since the version was
      compiled for an opcode of the family there*/
    uint64_t            family_mask, family_mask2;
    uint32_t            phys_2;
    uint32_t            owner; /*Number of the code block*/
    uint16_t            cs_sel; /*CS selector: x87 code stores it as the FPU CS*/
    uint16_t            status;
    uint32_t            flags; /*CODEBLOCK_HAS_FPU, CODEBLOCK_STATIC_TOP, CODEBLOCK_FAMILY_FIXED_CODE, CODEBLOCK_CALLS_C and CODEBLOCK_LINK_STATE of this version*/
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

/*The last cooldown armed by a block of a page. Code generated afresh each
  time it runs mostly starts new blocks, at whatever addresses its
  instructions fall on, and a new block gets no strikes of its own: one
  made on a page while a block of it is cooling down starts with that
  cooldown, and is interpreted rather than compiled until it ends.*/
#define SMC_PAGES 1024
typedef struct smc_page_t {
    uint32_t page;
    uint32_t strikes;
    uint64_t until;
} smc_page_t;
static smc_page_t smc_pages[SMC_PAGES];

/*The block codegen_check_flush() leaves alone: one that has just swapped
  its code for a kept version matching the bytes now there
  (codegen_block_swap_version())*/
static codeblock_t *check_flush_keep;

#ifdef CODEGEN_BACKEND_HAS_POLY
codegen_poly_t  codegen_poly[CODEGEN_POLY_NR];
/*The block each entry of codegen_poly[] is for*/
static uint32_t poly_owner[CODEGEN_POLY_NR];
/*The dirty bits of a poly block's next chunk on the next page: none, as
  its routine compares that chunk*/
static uint64_t poly_dirty_none;
#endif
static uint16_t        block_version_free_list;
static uint16_t        block_version_hand = 1;
/*Records in BLOCK_VERSION_KEPT: with none, a full pool has nothing to free*/
static int             block_versions_kept_nr;

static inline void
block_version_set_state(block_version_t *v, uint8_t state)
{
    block_versions_kept_nr += (state == BLOCK_VERSION_KEPT) - (v->state == BLOCK_VERSION_KEPT);
    v->state = state;
}
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

static void block_checked_drop_code(codeblock_t *block);
#endif
static void block_version_free(uint16_t nr);

static void
block_version_owner_sync(uint32_t block_nr)
{
    codegen_allocator_owner(block_nr, codeblock[block_nr].head_mem_block || block_version_kept[block_nr]);
}

static void
block_versions_init(void)
{
    memset(block_versions, 0, sizeof(block_versions));
    block_versions_kept_nr = 0;
    memset(block_version_cur, 0, sizeof(block_version_cur));
    memset(block_version_kept, 0, sizeof(block_version_kept));
    for (int c = 1; c <= BLOCK_VERSION_NR; c++)
        block_versions[c].next = (c < BLOCK_VERSION_NR) ? (c + 1) : 0;
    block_version_free_list = 1;
    block_version_hand      = 1;
}

/*Whether every family opcode the block's current version was compiled for
  is still one of the eight: then a slot changed kind, which a retime keeps
  from missing again. An opcode out of the family misses whatever the
  timing, and a retime would only cost the block its fixed slots
  (codegen_alu_family_eax_imm()). 1 where there is no version to tell by*/
static int
codegen_family_miss_is_kind(codeblock_t *block)
{
    const uint16_t         cur = block_version_cur[get_block_nr(block)];
    const block_version_t *v;

    if (!cur)
        return 1;
    v = &block_versions[cur];
    for (int chunk = 0; chunk < 2; chunk++) {
        const uint64_t mask = chunk ? v->family_mask2 : v->family_mask;
        const uint32_t phys = chunk ? v->phys_2 : block->phys;
        const page_t  *page;

        if (!mask)
            continue;
        page = &pages[phys >> 12];
        if ((page->mem == NULL) || (page->mem == page_ff))
            return 1;
        for (int c = 0; c < 64; c++) {
            if (((mask >> c) & 1) && codegen_alu_family_bad_c[page->mem[(phys & 0xfc0) + c]])
                return 0;
        }
    }
    return 1;
}

/*A CODEBLOCK_ALU_FAMILY block's start found an opcode out of the family:
  the block is marked dirty, to take a version compiled for what is there
  now, whose family opcodes are compared as the rest of its code is, or to
  be compiled again.*/
void
codegen_alu_family_miss(codeblock_t *block)
{
    page_t *page = &pages[block->phys >> 12];

    codegen_poly_drop(block);
    block->family_miss     = 0;
    block->family_miss_tsc = tsc;
    /*A slot that changed kind: the next compile takes all eight in every
      slot, and times new kinds as it runs. Not when it missed only to give
      up its fixed slots (CODEBLOCK_FAMILY_NOFIX), nor for an opcode out of
      the family*/
    {
        if (codegen_family_nofix_block == block)
            codegen_family_nofix_block = NULL;
        else if (codegen_family_miss_is_kind(block)) {
            const uint32_t block_nr = get_block_nr(block);
            const uint16_t cur      = block_version_cur[block_nr];

            block->flags |= CODEBLOCK_FAMILY_RETIME;
            /*Its current version, on the same bytes, would miss again if
              it were put back: not kept. Its code goes with the block's*/
            if (cur) {
                block_version_free(cur);
                block_version_cur[block_nr] = 0;
            }
        }
    }
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    /*A checked block's dirty pointer is the shared untracked mask, which
      writes never set and nothing may: it is left without code instead, and
      the loop puts back another version or compiles it again where it is*/
    if (block->dirty_mask == &block_untracked_mask) {
        if (block_checked_has_code(block)) {
            block_checked_drop_code(block);
            block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
            codegen_link_block_changed(block);
#    endif
        }
        return;
    }
#endif
    /*Its first byte left out of its code (codegen_block_end_recompile()): that
      is the opcode now out of the family, or code again, for the write to
      reach the block*/
    if (!block->page_mask && !block->poly)
        block->page_mask = (uint64_t) 1 << (block->pc & PAGE_MASK_MASK);
    if (block->dirty_mask) {
        *block->dirty_mask |= block->page_mask ? block->page_mask : 1;
        page->dirty_mask |= (uint64_t) 1 << ((block->phys >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
    }
}

/*The block leaves its trampoline (codegen.h): it gets back the code of the
  version it held as it went in and that version's bytes, marked dirty, so
  that the next visit finds out which version the chunk holds now.*/
void
codegen_poly_drop(codeblock_t *block)
{
#ifdef CODEGEN_BACKEND_HAS_POLY
    const uint32_t block_nr = get_block_nr(block);
    const uint16_t cur      = block_version_cur[block_nr];
    page_t        *page     = &pages[block->phys >> 12];

    if (!block->poly)
        return;
    if (block->dirty_mask2 == &poly_dirty_none)
        block->dirty_mask2 = &pages[block->phys_2 >> 12].byte_dirty_mask[(block->phys_2 >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK];
    codegen_poly[block->poly - 1].nr   = 0;
    codegen_poly[block->poly - 1].miss = 0;
    block->poly                        = 0;
    if (block->head_mem_block)
        block->data = codeblock_allocator_get_ptr(block->head_mem_block);
    block->page_mask = cur ? block_versions[cur].page_mask : 0;
    if (block->page_mask && block->dirty_mask) {
        *block->dirty_mask |= block->page_mask;
        page->dirty_mask |= (uint64_t) 1 << ((block->phys >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
    }
    /*and its next chunk's, which poly_enter() cleared with it*/
    block->page_mask2 = cur ? block_versions[cur].page_mask2 : 0;
    if (block->page_mask2 && block->dirty_mask2) {
        *block->dirty_mask2 |= block->page_mask2;
        pages[block->phys_2 >> 12].dirty_mask |= (uint64_t) 1 << ((block->phys_2 >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
    }
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#    endif
#else
    (void) block;
#endif
}

static void
block_version_free(uint16_t nr)
{
    block_version_t *v = &block_versions[nr];

    if (v->head_mem_block && (v->owner < BLOCK_SIZE) && codeblock[v->owner].poly)
        codegen_poly_drop(&codeblock[v->owner]);

    if ((v->state == BLOCK_VERSION_KEPT) && v->head_mem_block)
        codegen_allocator_free_version(v->head_mem_block);
    v->head_mem_block       = NULL;
    block_version_set_state(v, BLOCK_VERSION_FREE);
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
        if (!block_versions_kept_nr)
            return 0;
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

/*The physical address of a byte-mask block's second chunk, the 64 bytes
  after its first: on the block's page, or through the next page's
  translation (0xffffffff where there is none)*/
static uint32_t
block_version_phys_2(const codeblock_t *block)
{
    if ((block->pc & 0xfff) < 0xfc0)
        return block->phys + 0x40;
    return get_phys_noabrt(block->pc + 0x40);
}

static int
block_version_chunk_matches(uint32_t phys, const uint8_t *exec, uint64_t mask, const uint8_t *code)
{
    const uint8_t *fetch = _mem_exec[phys >> MEM_GRANULARITY_BITS];

    if (!fetch || (fetch != exec))
        return 0;
    fetch = &fetch[(phys & MEM_GRANULARITY_MASK) & ~0x3f];
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    /*Eight bytes at a time: each mask byte spread to one bit per byte*/
    for (int c = 0; c < 64; c += 8) {
        uint64_t m = (mask >> c) & 0xff;
        uint64_t a;
        uint64_t b;

        if (!m)
            continue;
        m = (m | (m << 28)) & 0x0000000f0000000full;
        m = (m | (m << 14)) & 0x0003000300030003ull;
        m = ((m | (m << 7)) & 0x0101010101010101ull) * 0xff;
        memcpy(&a, &fetch[c], 8);
        memcpy(&b, &code[c], 8);
        if ((a ^ b) & m)
            return 0;
    }
#else
    for (int c = 0; c < 64; c++) {
        if ((mask & ((uint64_t) 1 << c)) && (fetch[c] != code[c]))
            return 0;
    }
#endif
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

#    ifdef CODEGEN_BACKEND_HAS_LINK_TABLE
codegen_link_slot_t codegen_link_table[CODEGEN_LINK_TABLE_SIZE] __attribute__((aligned(64)));
#    endif

void
codegen_link_reset(void)
{
    memset(codegen_link_slots, 0, sizeof(codegen_link_slots));
#    ifdef CODEGEN_BACKEND_HAS_LINK_SETS
    link_sets_reset();
#    endif
#    ifdef CODEGEN_BACKEND_HAS_LINK_TABLE
    memset(codegen_link_table, 0, sizeof(codegen_link_table));
#    endif
}

void
codegen_link_block_changed(codeblock_t *block)
{
    if (!codegen_block_link)
        return;

#    if defined(__aarch64__) || defined(_M_ARM64)
    /*The arm64 link routine checks link_gen on every link, the short way's
      included, so a change to one block only moves its own link_gen on, not
      the epoch every record is checked at: code rewritten between versions
      would otherwise send every record round the full way after it*/
#    else
    codegen_link_epoch_bump();
#    endif
#    if defined(__aarch64__) || defined(_M_ARM64)
    /*Its exits' slots may leave oldcs and oldcpl only for the code they were
      filled for*/
    for (int c = 0; block->slots_used && (c < CODEGEN_LINK_SLOTS); c++) {
        codegen_link_slots[get_block_nr(block)][c].two_page &= 9;
#        ifdef CODEGEN_BACKEND_HAS_LINK_SETS
        /*and the records kept in its sets, which the routine runs on too*/
        {
            const uint16_t set = codegen_link_site_set[get_block_nr(block) * CODEGEN_LINK_SLOTS + c];

            if (set && (set != CODEGEN_LINK_SET_ONE)) {
                for (int n = 0; n < 3; n++)
                    codegen_link_sets[set].e[n].two_page &= 9;
            }
        }
#        endif
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
    if (!block->slots_used)
        return;
    block->slots_used = 0;
#    ifdef CODEGEN_BACKEND_HAS_LINK_SETS
    for (int c = 0; c < CODEGEN_LINK_SLOTS; c++)
        link_site_release(get_block_nr(block) * CODEGEN_LINK_SLOTS + c);
#    endif
    memset(codegen_link_slots[get_block_nr(block)], 0, sizeof(codegen_link_slots[0]));
}

/*Every field of a record but set, entry last*/
static inline void
link_record_fill(codegen_link_slot_t *r, codeblock_t *block, uint32_t phys, uint32_t virt, uintptr_t *xlat, uintptr_t xlat_val)
{
    r->exit_pc    = cpu_state.pc;
    r->exit_cs    = cs;
    r->gen        = block->link_gen;
    /*Without the bits the link routines compare it without
      (CPU_STATUS_FPU_PC24, CPU_STATUS_FPU_RC_NZ)*/
    r->status     = cpu_cur_status & ~(CPU_STATUS_FPU_PC24 | CPU_STATUS_FPU_RC_NZ);
    r->npxc       = cpu_state.npxc & 0x0f00;
#    if defined(__aarch64__) || defined(_M_ARM64)
    /*and the CPL it was filled at, which the link routine compares as well:
      an entry made at ring 0 is never used from ring 3, so entering it
      (flushmmucache_ring3()) needs no new epoch*/
    r->cr0_bits   = (cr0 & 0xc0000000) | CPL;
#    else
    r->cr0_bits   = cr0 & 0xc0000000;
#    endif
    r->phys_page  = phys & ~0xfff;
#    if defined(__aarch64__) || defined(_M_ARM64)
    (void) virt;
    (void) xlat;
    (void) xlat_val;
    r->end_slot   = block->end_slot % CODEGEN_LINK_SLOTS;
    /*Filled from the block the loop is about to run, at this pc's
      translation: every check the epoch stands for passes now. A link that
      then has to translate the address afresh, where the lookups no longer
      hold it, would leave to C every time until one passes the lot*/
    r->epoch      = codegen_link_epoch;
#    else
    r->block      = block;
    r->virt       = virt;
    r->rammask    = rammask;
    r->xlat       = xlat;
    r->xlat_val   = xlat_val;
    r->mem_exec   = &_mem_exec[phys >> MEM_GRANULARITY_BITS];
    r->hash       = &codeblock_hash[HASH(phys)];
#    endif
    r->block_nr   = get_block_nr(block);
    r->top_mask   = (block->flags & CODEBLOCK_STATIC_TOP) ? 7 : 0;
    r->top        = (block->flags & CODEBLOCK_STATIC_TOP) ? block->TOP : 0;
    r->two_page   = !!block->page_mask2;
#    if defined(__aarch64__) || defined(_M_ARM64)
    /*Long enough for the link routine to prefetch the next block's code*/
    if (block->ins >= CODEGEN_LINK_NEXT_MIN_INS)
        r->two_page |= 8;
#    endif
    r->page_mask  = block->page_mask;
    r->dirty_mask = block->dirty_mask;
    r->entry      = &block->data[BLOCK_START + CODEGEN_LINK_ENTRY_OFFSET];
}

#    if defined(__aarch64__) || defined(_M_ARM64)
/*The arm64 link routine's way past a readlookup2[] miss with paging on:
  what the C loop's get_phys() would do next, the page walk setting the
  Accessed bits and filling the lookup, done here only when that walk cannot
  fault, which a walk that touches nothing checks first (for a read it
  tests what the real one does). Returns the page get_phys() gives, or
  0xffffffff (page unaligned, no record's) where the walk would fault: the
  C loop takes that. After every TLB flush (a mode switch's, CR3, INVLPG)
  each exit to code on another page went back to the C loop for the walk
  alone*/
uint32_t
codegen_link_walk(uint32_t addr)
{
    if (mmutranslate_noabrt(addr, 0) == 0xffffffffffffffffULL)
        return 0xffffffff;
    return get_phys(addr) & ~0xfff;
}
#    endif

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
#    if (defined(__aarch64__) || defined(_M_ARM64)) && defined(CODEGEN_BACKEND_HAS_ENTRY_CHECK)
    /*A slot tests the dirty words its block's pointers name, which writes
      never set for one compared on entry instead (codegen_block_check())*/
    if ((block->dirty_mask == &block_untracked_mask) || (block->dirty_mask2 == &block_untracked_mask))
        return;
#    endif
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

#    if defined(__aarch64__) || defined(_M_ARM64)
    /*An exit back to a loop's head just before its block's start, which the
      recording would have followed had the branch gone there then (the loop
      was not running when the block was compiled): the block is compiled
      again, once for its address, to go round the loop itself rather than
      through a block of its own. Not a byte-mask block, which never follows
      back*/
    if ((from >= &codegen_link_slots[0][0]) && (from < (&codegen_link_slots[0][0] + (size_t) BLOCK_SIZE * CODEGEN_LINK_SLOTS))) {
        static uint32_t rerecorded[256];
        codeblock_t    *src = &codeblock[(from - &codegen_link_slots[0][0]) / CODEGEN_LINK_SLOTS];
        uint32_t       *done = &rerecorded[(src->phys ^ (src->phys >> 8)) & 0xff];

        if ((virt < src->pc) && ((src->pc - virt) <= CODEGEN_JCC_BACK) && !((virt ^ src->pc) & ~0xfff) &&
            ((src->flags & (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_IN_DIRTY_LIST | CODEBLOCK_BYTE_MASK)) == CODEBLOCK_WAS_RECOMPILED) &&
            (src != block) && (*done != src->phys + 1)) {
            *done = src->phys + 1;
            src->flags &= ~CODEBLOCK_WAS_RECOMPILED;
            codegen_link_block_changed(src);
            return;
        }
    }
#    endif

#    if defined(__aarch64__) || defined(_M_ARM64)
    /*The anchor of an idle loop's watch: each arrival there comes back to C
      (386_dynarec.c)*/
    {
        extern codeblock_t *idle_anchor;

        if (block == idle_anchor)
            return;
    }
#    endif

    /*The exit's own slot, whatever it held*/
    slot = from;
#    ifdef CODEGEN_BACKEND_HAS_LINK_SETS
    if (slot->entry && ((slot->exit_pc != cpu_state.pc) || (slot->exit_cs != cs)))
        link_set_keep(slot);
#    endif

    slot->entry = NULL;
    link_record_fill(slot, block, phys, virt, xlat, xlat_val);
    /*One of a block's own slots: link_slots_clear() empties them, and gives
      back the sets of their sites, when the block goes*/
    if ((slot >= &codegen_link_slots[0][0]) && (slot < (&codegen_link_slots[0][0] + (size_t) BLOCK_SIZE * CODEGEN_LINK_SLOTS))) {
        codeblock_t *src = &codeblock[(slot - &codegen_link_slots[0][0]) / CODEGEN_LINK_SLOTS];

        src->slots_used = 1;
#    if defined(__aarch64__) || defined(_M_ARM64)
        /*the block calling no C: CS and the CPL are as at the loop top when
          it exits (codegen_link_slot_t::two_page)*/
        if (!(src->flags & CODEBLOCK_CALLS_C))
            slot->two_page |= 2;
        if (!(src->flags & (CODEBLOCK_CALLS_C | CODEBLOCK_LINK_STATE))) {
            slot->two_page |= 4;
            /*and to a target without a static TOP: the link routine's
              short way*/
            if (!(block->flags & CODEBLOCK_STATIC_TOP))
                slot->two_page |= 32;
        }
#    endif
    }
#    ifdef CODEGEN_BACKEND_HAS_LINK_TABLE
    /*Filled the same way rather than copied from the slot, whose fields
      were just stored one by one*/
    link_record_fill(&codegen_link_table[CODEGEN_LINK_TABLE_HASH(cpu_state.pc, cs)], block, phys, virt, xlat, xlat_val);
#    endif
}
#else
const int codegen_has_block_link = 0;
#endif

static void
block_free_list_add(codeblock_t *block)
{
    codegen_poly_drop(block);
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
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
        /* A block compiled in byte-mask form that can have versions is not
           deleted: it is made checked, without code and off the lists as
           block_checked_invalidate() leaves one, keeping its versions. Its
           next entry (codegen_block_check()) puts back the kept version
           compiled from the bytes there or compiles it with the entry
           check. Its masks are per byte, as the entry tests read them. */
        if (((evict_block->flags & (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_BYTE_MASK)) == (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_BYTE_MASK)) &&
            !evict_block->head_mem_block && ((evict_block->pc & 0xfff) < 0xfc0)) {
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

/*The evict list is otherwise only flushed when the blocks run out
  (codegen_purge_purgable_list()): a page whose code was written over as
  data, never to run again, kept its dead blocks, and every write to it took
  the tracked path. The C loop calls this at most once every
  CODEGEN_PURGE_INTERVAL cycles: the list's head page is flushed as a lookup
  would flush it, taken off the list, and once it holds no block its writes
  go direct again (mem_unflush_write_page()). Code being rewritten as it
  runs is flushed at most once an interval before its next lookup would*/
uint64_t codegen_purge_tsc;

void
codegen_purge_stale_page(void)
{
    const uint32_t nr   = purgable_page_list_head;
    page_t        *page = &pages[nr];

    codegen_purge_tsc = tsc;
    codegen_check_flush(page, page->dirty_mask, nr << 12);
    if (page_in_evict_list(page))
        page_remove_from_evict_list(page);
    if (!page->block && !page->block_2 && ((nr << 12) != recomp_page))
        mem_unflush_write_page(nr << 12);
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
            block->used = 0;
            continue;
        }
        block_clock_hand = (block_nr + 1) & BLOCK_MASK;
        delete_block(block);
        return 1;
    }
    block_clock_hand = block_nr;
    return 0;
}

static codeblock_t *
block_free_list_get(void)
{
    codeblock_t *block = NULL;

    while (!block_free_list) {
        /*Free list is empty: a block not run lately first*/
        if (block_recycle_stale())
            continue;
        /*Then the dirty list*/
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

    codegen_alu_family_init();
    codegen_backend_init();
    codeblock_dec = calloc(BLOCK_SIZE, sizeof(*codeblock_dec));
    if (!codeblock_dec)
        fatal("codegen_init: out of memory\n");
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

uint32_t codeblock_chain[1 << CODEBLOCK_CHAIN_BITS];

void
codegen_reset(void)
{
    int c;

    /*The page table may already have been replaced. Drop its entry numbers
      before unlinking the old blocks, then reset the chunk table at once.*/
    if (block_chunk_page)
        memset(block_chunk_page, 0, block_chunk_page_nr * sizeof(uint32_t));
    memset(smc_pages, 0, sizeof(smc_pages));
#ifdef CODEGEN_BACKEND_HAS_POLY
    memset(codegen_poly, 0, sizeof(codegen_poly));
#endif

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
    memset(codeblock_chain, 0, sizeof(codeblock_chain));
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

/*The chunks of a listed block's page (second: of its second page) whose
  writes it checks for: a byte-mask block's one chunk there (its dirty mask
  is that chunk's byte mask), any other block's page_mask (page_mask2).
  They stay in page_t::live_chunks after the block is gone*/
static inline uint64_t
block_live_chunks(const codeblock_t *block, int second)
{
    if (block->flags & CODEBLOCK_BYTE_MASK)
        return (uint64_t) 1 << (((second ? block->phys_2 : block->phys) >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK);
    return second ? block->page_mask2 : block->page_mask;
}

static void
add_to_block_list(codeblock_t *block)
{
    uint32_t block_prev_nr = pages[block->phys >> 12].block;
    uint32_t block_nr      = get_block_nr(block);

#ifndef RELEASE_BUILD
    if (!block->page_mask && !(block->flags & CODEBLOCK_BYTE_MASK))
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
    pages[block->phys >> 12].live_chunks |= block_live_chunks(block, 0);

    if (block->next) {
#ifndef RELEASE_BUILD
        if (!codeblock[block->next].valid)
            fatal("!block->next->valid %p %p %x %x\n", (void *) &codeblock[block->next], (void *) codeblock, block_current, block_pos);
#endif
    }

    if (block->page_mask2) {
        block->flags |= CODEBLOCK_HAS_PAGE2;
        pages[block->phys_2 >> 12].live_chunks |= block_live_chunks(block, 1);

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
        /* The page's last block going leaves its writes tracked: they
           become direct again as its write lookups are refilled. Flushing
           them here cost two flushes and two refills for every rewrite of
           a page's only block. */
        pages[block->phys >> 12].block = block->next;
        if (block->next)
            codeblock[block->next].prev = BLOCK_INVALID;
        else
            block_chunk_lists_release(block->phys >> 12);
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
        else
            block_chunk_lists_release(block->phys_2 >> 12);
    }
}

/*Strike threshold, base cooldown length (in tsc counts), and cap on the
  exponential backoff applied per consecutive strike, for the SMC-thrash
  interpret fallback (see page_t).*/
#define SMC_THRASH_STRIKE_THRESHOLD 3
#define SMC_THRASH_BASE_COOLDOWN    4096
#define SMC_THRASH_MAX_STRIKES      16

/* A block that compiled, then got rewritten before it ran often enough
   from block->data to make up for compiling it, cost more than
   interpreting it would have: count it as a strike against the block.
   One that ran often enough paid for itself, so it clears the block's
   strikes instead. One that was only marked, never compiled, cost
   nothing extra and tells neither. */
static void
block_smc_account(codeblock_t *block)
{
    if (block->flags & CODEBLOCK_WAS_RECOMPILED) {
        if (block->runs_left) {
            if (block->smc_strikes < SMC_THRASH_MAX_STRIKES)
                block->smc_strikes++;
            if (block->smc_strikes >= SMC_THRASH_STRIKE_THRESHOLD) {
                smc_page_t *sp = &smc_pages[(block->phys >> 12) & (SMC_PAGES - 1)];

                block->smc_cooldown_until = tsc + ((uint64_t) SMC_THRASH_BASE_COOLDOWN << (block->smc_strikes - SMC_THRASH_STRIKE_THRESHOLD));
                sp->page                  = block->phys >> 12;
                sp->strikes               = block->smc_strikes;
                sp->until                 = block->smc_cooldown_until;
            }
        } else
            block->smc_strikes = 0;
    }
}

static void
invalidate_block(codeblock_t *block)
{
    uint32_t old_pc = block->pc;

    codegen_poly_drop(block);

#ifndef RELEASE_BUILD
    if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
        fatal("invalidate_block: already in dirty list\n");
    if (!block->valid)
        fatal("Invalidating deleted block\n");
#endif
#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    /* Rewritten again after it was compiled to read its immediates from
       memory: its next compiles get the entry check, and while its current
       code has one, writes stop marking the block and the check compares its
       own bytes with memory on every entry instead (codegen_block_check()).
       Only a block with versions, whose two chunks are in one page. */
    if (((block->flags & (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_BYTE_MASK | CODEBLOCK_NO_IMMEDIATES)) ==
         (CODEBLOCK_WAS_RECOMPILED | CODEBLOCK_BYTE_MASK | CODEBLOCK_NO_IMMEDIATES)) &&
        ((block->pc & 0xfff) < 0xfc0))
        block->flags |= CODEBLOCK_CHECKED;
#endif
    block_smc_account(block);
    codegen_block_note_written(block);
    remove_from_block_list(block, old_pc);
    block_dirty_list_add(block);
    if (block->head_mem_block) {
        uint32_t block_nr = get_block_nr(block);
        uint16_t cur      = block_version_cur[block_nr];

        if (cur && (block_versions[cur].head_mem_block == block->head_mem_block)) {
            /*Keep it, newest first, and no more than BLOCK_VERSION_PER_BLOCK*/
            uint16_t nr;
            int      c = 1;

            block_version_set_state(&block_versions[cur], BLOCK_VERSION_KEPT);
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
    /*The block's exit slots are kept: a version put back has the same exits
      to the same places more often than not, and every record is checked
      against the exit's key and its target's link_gen before it is used*/
}

static void
delete_block(codeblock_t *block)
{
    uint32_t old_pc = block->pc;

    codegen_poly_drop(block);

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
                block->used = 0;
                block_nr    = (block_nr + 1) & BLOCK_MASK;
                continue;
            }
            if (block->valid && (!required_mem_block || block->head_mem_block)) {
                block_clock_hand = (block_nr + 1) & BLOCK_MASK;
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

/*A block on the page's first list, as the flush tests and invalidates it*/
static void
check_flush_block(codeblock_t *block)
{
    if (block == check_flush_keep)
        return;
    if (*block->dirty_mask & block->page_mask) {
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
    if (block == check_flush_keep)
        return;
    if (*block->dirty_mask2 & block->page_mask2) {
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

    /* A page with no RAM behind it has no byte masks of its own. Every
       chunk is cleared: a checked block's entry tests read its chunks' dirty
       bits, which no dirty_mask bit need show (codegen_block_check()). */
    for (uint8_t c = 0; (c < 64) && (page->mem != page_ff); c++) {
        if (page->byte_code_present_mask[c] & page->byte_dirty_mask[c])
            remove_from_evict_list = 0;
        page->byte_code_present_mask[c] &= ~page->byte_dirty_mask[c];
        page->byte_dirty_mask[c] = 0;
    }
    /*codegen_alu_family_miss() and codegen_poly_drop() mark code dirty
      without putting its page on the evict list, as a write does*/
    if (remove_from_evict_list && page_in_evict_list(page))
        page_remove_from_evict_list(page);
}

/*The bytes written in the block's first two chunks since they were last
  flushed, added to codeblock_t::smc_written*/
void
codegen_block_note_written(codeblock_t *block)
{
    const page_t *page = &pages[block->phys >> 12];
    const int     c    = (block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

    if (!page->mem || (page->mem == page_ff) || !page->byte_dirty_mask)
        return;
    block->smc_written[0] |= page->byte_dirty_mask[c];
    if (c < 63)
        block->smc_written[1] |= page->byte_dirty_mask[c + 1];
    else if ((block->flags & CODEBLOCK_BYTE_MASK) && block->page_mask2 && (block->phys_2 != 0xffffffff)) {
        /*The second chunk on the next page*/
        const page_t *page_2 = &pages[block->phys_2 >> 12];

        if (page_2->mem && (page_2->mem != page_ff) && page_2->byte_dirty_mask)
            block->smc_written[1] |= page_2->byte_dirty_mask[(block->phys_2 >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK];
    }
}

/*A byte-mask block past the first two chunks it marks (codeblock_t::
  page_mask, page_mask2) goes on with family instructions only, read as it
  runs, within its first page and the block size limit: 1 if the instruction
  at addr is one*/
int
codegen_family_beyond_ok(const codeblock_t *block, uint32_t addr)
{
    uint8_t opcode;

    if (((block->flags & (CODEBLOCK_ALU_FAMILY | CODEBLOCK_NO_IMMEDIATES)) != (CODEBLOCK_ALU_FAMILY | CODEBLOCK_NO_IMMEDIATES)) ||
        ((addr - block->pc) >= (1000 - 16)) || (((addr + 4) ^ block->pc) & ~0xfff) ||
        (codegen_alu_family_nr >= CODEGEN_ALU_FAMILY_MAX))
        return 0;
    opcode = fastreadb(addr);
    if (cpu_state.abrt)
        return 0;
    return ((opcode & 0xc7) == 0x05);
}

/*A sum of the chunks a byte-mask block may cover on its first page, from its
  first*/
uint32_t
codegen_block_chunk_sum(const codeblock_t *block)
{
    const page_t   *page = &pages[block->phys >> 12];
    const uint32_t  off  = block->phys & 0xfc0;
    /*The chunks a block can reach on its page (codegen_family_beyond_ok())*/
    const int       nr   = ((0x1000 - off) < 1024) ? ((0x1000 - off) >> 3) : 128;
    uint64_t        sum  = 0;
    uint64_t        q;

    if (!page->mem || (page->mem == page_ff))
        return 0;
    for (int c = 0; c < nr; c++) {
        memcpy(&q, &page->mem[off + (c << 3)], 8);
        sum = (sum ^ q) * 0x9e3779b97f4a7c15ull;
    }
    return (uint32_t) (sum >> 32);
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
    block->warm_left         = CODEGEN_WARM_RUNS;
    block->dec_n             = 0;
    block->smc_strikes        = 0;
    block->smc_written[0] = block->smc_written[1] = 0;
    block->smc_cooldown_until = 0;
    block->swap_misses        = 0;
    block->swap_off_until     = 0;
    {
        const smc_page_t *sp = &smc_pages[(phys_addr >> 12) & (SMC_PAGES - 1)];

        if ((sp->page == (phys_addr >> 12)) && (sp->until > tsc)) {
            block->smc_strikes        = sp->strikes;
            block->smc_cooldown_until = sp->until;
        }
    }
    block->pc                = cs + cpu_state.pc;
    block->_cs         = cs;
    block->phys        = phys_addr;
    block->dirty_mask  = &page->dirty_mask;
    block_chk_dirty_sync(block);
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

#ifdef CODEGEN_BACKEND_HAS_FAR_PAGE2
/*The pages (linear address >> 12) the block being recorded is on: its
  start's, and a second one once the recording reaches another*/
static uint32_t record_page_1;
static uint32_t record_page_2;
static int      record_page_2_set;

/*An instruction at addr may be recorded into the block: its first byte,
  and its last if it can cross a page boundary (15 bytes at most), are on
  the block's pages, the second one taken now if set and there is none yet.
  Else the block must end before it. A byte-mask block keeps to the 64
  bytes after its chunk, as before (the recording loop's size check)*/
int
codegen_record_page_ok(uint32_t addr, int set)
{
    const uint32_t page = addr >> 12;
    const uint32_t last = (addr + 14) >> 12;
    uint32_t       other;

    if (codeblock[block_current].flags & CODEBLOCK_BYTE_MASK)
        return 1;
    if (record_page_2_set)
        return ((page == record_page_1) || (page == record_page_2)) && ((last == record_page_1) || (last == record_page_2));
    /*At most one page apart from the first between them*/
    other = (page != record_page_1) ? page : last;
    if (((page != record_page_1) && (page != other)) || ((last != record_page_1) && (last != other)))
        return 0;
    if (set && (other != record_page_1)) {
        record_page_2     = other;
        record_page_2_set = 1;
    }
    return 1;
}

/*An instruction at addr is on pages the block being recorded has already
  (codegen_record_page_ok() with no page left to take). Never for a
  byte-mask block*/
int
codegen_record_page_held(uint32_t addr)
{
    const uint32_t page = addr >> 12;
    const uint32_t last = (addr + 14) >> 12;

    if (codeblock[block_current].flags & CODEBLOCK_BYTE_MASK)
        return 0;
    return ((page == record_page_1) || (record_page_2_set && (page == record_page_2))) &&
        ((last == record_page_1) || (record_page_2_set && (last == record_page_2)));
}
#endif

void
codegen_block_start_recompile(codeblock_t *block)
{
    page_t *page = &pages[block->phys >> 12];

    codegen_imm_page2 = 0;
    /*Set as the block is recorded if its own stores change its code
      (codegen_can_unroll_full())*/
    cpu_state.code_written = 0;
    codegen_poly_drop(block);
#ifdef CODEGEN_BACKEND_HAS_FAR_PAGE2
    record_page_1     = block->pc >> 12;
    record_page_2_set = 0;
#endif
    block->smc_swaps    = 0;
    block->family_miss  = 0;
    block->flags &= ~CODEBLOCK_FAMILY_FIXED_CODE;
    if ((tsc - block->family_miss_tsc) > (1ull << 24))
        block->flags &= ~(CODEBLOCK_FAMILY_RETIME | CODEBLOCK_FAMILY_NOFIX);
    codegen_link_state_ins = 0;
    codegen_flags_debt_ins = 0;
    codegen_alu_family_nr = 0;
    codegen_alu_family_prev_ins = -2;

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
    /*The second chunk is the next 64 bytes, on this page or through the next
      page's translation (block_version_phys_2()): a version is put back only
      where the bytes are the snapshot again through the same mappings*/
    block_version_start_ok = 0;
    if ((block->flags & CODEBLOCK_BYTE_MASK) && (block->flags & CODEBLOCK_NO_IMMEDIATES)) {
        block_version_start_phys_2 = block_version_phys_2(block);
        if (block_version_start_phys_2 != 0xffffffff) {
            block_version_start_exec   = block_version_read_chunk(block->phys, block_version_start_code);
            block_version_start_exec_2 = block_version_read_chunk(block_version_start_phys_2, &block_version_start_code[64]);
            block_version_start_cs     = CS;
            block_version_start_ok     = (block_version_start_exec != NULL) && (block_version_start_exec_2 != NULL);
        }
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
    block->runs_left = SMC_THRASH_RUN_MASK; /* not yet run from block->data */
    block->compiled_tsc = tsc;
    block->smc_sum      = codegen_block_chunk_sum(block);

    block->page_mask = block->page_mask2 = 0;
    block->ins                           = 0;

    cpu_block_end = 0;

    codegen_block_cycles = 0;
    codegen_followed_near = 0;
    codegen_follow_reset();
    /*Before the retime wraps the timing model: it wraps the null one then*/
    codegen_nocache_begin();
#ifdef CODEGEN_BACKEND_HAS_ALU_FAMILY
    codegen_family_retime_begin(block);
#endif
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
    block->flags |= CODEBLOCK_WAS_RECOMPILED;
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
    /*Writes do not mark a block whose current code is a version with the
      entry check: that compares its bytes on every entry instead*/
    const int untracked = block_version_cur[block_current] && block_versions[block_version_cur[block_current]].checked;

    p = &pages[block->phys >> 12];
    page_store_fast_clear(p);
    if (block->flags & CODEBLOCK_BYTE_MASK) {
        int offset = (block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

        /*Marked as code either way: devices that write RAM directly mark
          only bytes marked as code (mem_phys_has_code()). None of its first
          chunk may be code (its family opcodes and the immediates are read
          as it runs): its first byte is marked present all the same, for
          the lookup to find the block*/
        p->byte_code_present_mask[offset] |= block->page_mask ? block->page_mask : ((uint64_t) 1 << (block->phys & PAGE_BYTE_MASK_MASK));
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
          next page. Not codegen_endpc, which guesses where the last
          instruction ends from where it starts: one of 11 bytes or more
          marks bytes past the guess, in a chunk or page it doesn't name.*/
        uint32_t pc_2 = block->pc + ((block->flags & CODEBLOCK_BYTE_MASK) ? 0x40 : 0x400);

#ifdef CODEGEN_BACKEND_HAS_FAR_PAGE2
        if (!(block->flags & CODEBLOCK_BYTE_MASK) && record_page_2_set)
            pc_2 = record_page_2 << 12;
#endif
        block->pc_2 = pc_2;

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
    page_store_fast_clear(p);
    p->code_present_mask |= block->page_mask;
    if ((p->dirty_mask & block->page_mask) && !page_in_evict_list(p))
        page_add_to_evict_list(p);

    block->phys_2     = -1;
    block->page_mask2 = 0;
    block->next_2 = block->prev_2 = BLOCK_INVALID;
    if ((block->pc ^ codegen_endpc) & ~0xfff) {
        block->phys_2 = get_phys_noabrt(codegen_endpc);
        block->pc_2   = codegen_endpc;
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

    recomp_page = -1;
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

void
codegen_block_end_recompile(codeblock_t *block)
{
    /*An immediate read at run time from the second chunk's page wants that
      page's mapping checked at the lookup, as it is for a block with code
      there: one of its bytes is made code where no other is*/
    if (codegen_imm_page2 && !block->page_mask2)
        block->page_mask2 = (uint64_t) 1 << (codegen_imm_page2 - 1);
    codegen_imm_page2 = 0;

    uint64_t page_mask2_compiled = block->page_mask2;
    int      drain_from;

    codegen_accumulate_record(ACC_END, 0, 0, 0, 0, 0);
    drain_from = codegen_block_cycles;
    codegen_timing_block_end();
    codegen_exit_end_charge_apply(ir_data, codegen_block_cycles - drain_from);
#ifdef CODEGEN_BACKEND_HAS_ALU_FAMILY
    codegen_family_retime_lump(ir_data, block, !codegen_ir_unrolling());
#endif
    codegen_accumulate_timing(ir_data);

    /*A block that starts with family instructions (CODEBLOCK_ALU_FAMILY) may
      have none of its first chunk's bytes marked. Its first byte is then
      left out too when it is a family opcode, which the block checks as it
      starts, so a write turning it into another leaves the block standing;
      it is marked present below all the same, for the lookup to find the
      block. Any other first byte stays code.*/
    const int family_start = (block->flags & CODEBLOCK_ALU_FAMILY) && codegen_alu_family_nr &&
                             (codegen_alu_family_addr[0] == block->phys);
    if (!block->page_mask && !family_start)
        block->page_mask = (uint64_t) 1 << (block->pc & PAGE_MASK_MASK);
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

    if (!(block->flags & CODEBLOCK_HAS_FPU))
        block->flags &= ~CODEBLOCK_STATIC_TOP;

    /*The unrolled branch falls through when taken. Only its not-taken
      exit owes the drain and the branch credit.*/
    if (!codegen_ir_unrolling())
        codegen_accumulate_flush(ir_data);
#ifdef CODEGEN_META_PC
    /*The writes of pc and oldpc after the last uop (the last instruction's
      end), inside an unrolled loop's copies*/
    if (codegen_meta_ev)
        uop_meta_carrier(ir_data);
#endif
    codegen_ir_compile(ir_data, block);
#ifdef CODEGEN_BACKEND_HAS_ALU_FAMILY
    codegen_family_retime_end();
#endif
    codegen_nocache_end();
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#endif

#ifdef CODEGEN_BACKEND_HAS_ENTRY_CHECK
    if (block_version_checked_nr) {
        block_version_t *v = &block_versions[block_version_checked_nr];

        codegen_entry_check.nr                  = 0;
        v->flags                                = block->flags & (CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP);
        v->state                                = BLOCK_VERSION_CURRENT;
        block_version_cur[get_block_nr(block)] = block_version_checked_nr;
        return;
    }
#endif

    /*Record what this version was compiled from, so it can be kept when the
      guest rewrites the block: the bytes and mappings as at the start of the
      compile, from which every instruction was compiled
      (codegen_block_version_check_ins()). The block may have rewritten them
      as it ran here: then they are dirty and the version is kept at the
      next visit, for when the guest writes them back (code that patches
      its own first instruction each time it is entered).*/
    if (block_version_start_ok && (block->page_mask2 == page_mask2_compiled) &&
        (block->flags & CODEBLOCK_BYTE_MASK) && (block->flags & CODEBLOCK_NO_IMMEDIATES)) {
        uint16_t nr = block_version_alloc();

        if (nr) {
            block_version_t *v   = &block_versions[nr];
            uint64_t         fm  = 0;
            uint64_t         fm2 = 0;

            for (int c = 0; c < codegen_alu_family_nr; c++) {
                const uint32_t a = codegen_alu_family_addr[c];

                if ((a - (block->phys & ~0x3f)) >= 128)
                    continue;
                if (!((a ^ block->phys) & ~0x3f))
                    fm |= (uint64_t) 1 << (a & 0x3f);
                else
                    fm2 |= (uint64_t) 1 << (a & 0x3f);
            }
            v->exec   = block_version_start_exec;
            v->exec_2 = block->page_mask2 ? block_version_start_exec_2 : NULL;
            memcpy(v->code, block_version_start_code, sizeof(v->code));
            /*Family opcodes in a second chunk are compared through its
              mapping, which is only checked for one with code of its own*/
            if ((_mem_exec[block->phys >> MEM_GRANULARITY_BITS] == v->exec) && (!fm2 || block->page_mask2) &&
                (!block->page_mask2 ||
                 ((block->phys_2 == block_version_start_phys_2) &&
                  (_mem_exec[block->phys_2 >> MEM_GRANULARITY_BITS] == v->exec_2)))) {
                v->cs_sel                             = block_version_start_cs;
                v->head_mem_block                     = block->head_mem_block;
                v->page_mask                          = block->page_mask;
                v->page_mask2                         = block->page_mask2;
                v->family_mask                        = fm;
                v->family_mask2                       = fm2;
                v->phys_2                             = block->phys_2;
                v->owner                              = get_block_nr(block);
                v->status                             = block->status;
                /*With whether its code calls C or changes IF and TF, which
                  the link routine's slot hints follow*/
                v->flags                              = block->flags & (CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP | CODEBLOCK_FAMILY_FIXED_CODE | CODEBLOCK_CALLS_C | CODEBLOCK_LINK_STATE);
                v->TOP                                = block->TOP;
                v->checked = 0;
                block_version_set_state(v, BLOCK_VERSION_CURRENT);
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
            ((v->flags & CODEBLOCK_FAMILY_FIXED_CODE) && (block->flags & CODEBLOCK_FAMILY_NOFIX)) ||
            !block_version_chunk_matches(block->phys, v->exec, v->page_mask | v->family_mask, v->code)) {
            link = &v->next;
            continue;
        }
        if ((v->page_mask2 || v->family_mask2) && ((v->phys_2 != block_version_phys_2(block)) ||
                              !block_version_chunk_matches(v->phys_2, v->exec_2, v->page_mask2 | v->family_mask2, &v->code[64]))) {
            link = &v->next;
            continue;
        }

        /*Match: it becomes the block's current code, as a compile would*/
        *link                       = v->next;
        v->next                     = 0;
        block_version_set_state(v, BLOCK_VERSION_CURRENT);
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
        block_chk_dirty_sync(block);
        block->dirty_mask2    = NULL;
        block->flags          = (block->flags & ~(CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP | CODEBLOCK_FAMILY_FIXED_CODE | CODEBLOCK_CALLS_C | CODEBLOCK_LINK_STATE)) | v->flags | CODEBLOCK_WAS_RECOMPILED;
        /* Restoring a kept version is cheap (a few memcmp calls), unlike a
           real recompile: do not let it count as an SMC-thrash strike if
           it gets invalidated again before being run often from block->data. */
        block->runs_left      = 0;
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

#ifdef CODEGEN_BACKEND_HAS_POLY
/*A block that has swapped versions often since it was compiled goes over to
  a trampoline with up to CODEGEN_POLY_VERSIONS of them: the current one and
  the newest kept, all compiled for one chunk from the same mapping, with no
  x87 state of their own and the same status and CS.*/
static void
poly_enter(codeblock_t *block)
{
    const uint32_t   block_nr = get_block_nr(block);
    const uint16_t   cur      = block_version_cur[block_nr];
    block_version_t *vc       = &block_versions[cur];
    codegen_poly_t  *p        = NULL;
    int              idx;
    uint16_t         nr;
    uint16_t         hints    = 0;

    /*A block whose next chunk is on the next page keeps its page_mask2,
      for the lookups to check that page's translation as they do the
      block's own, with no dirty bits to see (poly_dirty_none)*/
    const int        cross    = block->page_mask2 && (block->flags & CODEBLOCK_BYTE_MASK) && ((block->phys_2 ^ block->phys) & ~0xfff);

    if ((block->flags & (CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP)) || (vc->flags & (CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP)) ||
        !vc->exec || !block->dirty_mask || (cross && (!block->dirty_mask2 || !vc->exec_2 || (vc->phys_2 != block->phys_2))))
        return;
    for (idx = 0; idx < CODEGEN_POLY_NR; idx++) {
        if (!codegen_poly[idx].nr) {
            p = &codegen_poly[idx];
            break;
        }
    }
    if (!p) {
        /*All taken: the next one round the pool goes back to its block's
          own code, as blocks that went over long ago would otherwise keep
          them all*/
        static int hand = 0;

        idx  = hand;
        hand = (hand + 1) % CODEGEN_POLY_NR;
        if (poly_owner[idx] == block_nr)
            return;
        codegen_poly_drop(&codeblock[poly_owner[idx]]);
        p = &codegen_poly[idx];
        if (p->nr)
            return;
    }

    p->miss     = 0;
    p->nr       = 0;
    p->mem_exec = (uint8_t **) &_mem_exec[block->phys >> MEM_GRANULARITY_BITS];
    p->exec     = vc->exec;
    p->code     = &vc->exec[(block->phys & MEM_GRANULARITY_MASK) & ~0x3f];
    /*The next chunk through its own mapping, on the next page for one
      crossing into it, which every version then has code in*/
    if (cross) {
        p->mem_exec_2 = (uint8_t **) &_mem_exec[block->phys_2 >> MEM_GRANULARITY_BITS];
        p->exec_2     = vc->exec_2;
        p->code_2     = &vc->exec_2[(block->phys_2 & MEM_GRANULARITY_MASK) & ~0x3f] - 64;
    } else {
        p->mem_exec_2 = p->mem_exec;
        p->exec_2     = p->exec;
        p->code_2     = p->code;
    }
    for (nr = cur; nr && (p->nr < CODEGEN_POLY_VERSIONS); nr = (nr == cur) ? block_version_kept[block_nr] : block_versions[nr].next) {
        const block_version_t  *v  = &block_versions[nr];
        codegen_poly_version_t *pv = &p->v[p->nr];

        /*A second chunk as the next 64 bytes, through the mapping the
          routine reads it through*/
        if ((cross ? (!v->page_mask2 || (v->phys_2 != block->phys_2) || (v->exec_2 != p->exec_2))
                   : (v->page_mask2 && ((v->phys_2 != (block->phys + 0x40)) || ((v->phys_2 ^ block->phys) & ~0xfff) || (v->exec_2 != vc->exec)))) ||
            (v->family_mask2 && !v->page_mask2) || (v->exec != vc->exec) || (v->cs_sel != vc->cs_sel) || (v->status != vc->status) ||
            (v->flags & (CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP)) || !v->head_mem_block || !v->page_mask ||
            ((v->flags & CODEBLOCK_FAMILY_FIXED_CODE) && (codeblock[block_nr].flags & CODEBLOCK_FAMILY_NOFIX)))
            continue;
        for (int w = 0; w < 16; w++) {
            const uint64_t mask = (w < 8) ? (v->page_mask | v->family_mask) : (v->page_mask2 | v->family_mask2);
            uint64_t       m    = (mask >> ((w & 7) * 8)) & 0xff;
            uint64_t       c;

            m = (m | (m << 28)) & 0x0000000f0000000full;
            m = (m | (m << 14)) & 0x0003000300030003ull;
            m = ((m | (m << 7)) & 0x0101010101010101ull) * 0xff;
            memcpy(&c, &v->code[w * 8], 8);
            pv->m[w] = m;
            pv->c[w] = c & m;
        }
        pv->two = !!(v->page_mask2 | v->family_mask2);
        hints |= v->flags & (CODEBLOCK_CALLS_C | CODEBLOCK_LINK_STATE);
        pv->entry = codeblock_allocator_get_ptr(v->head_mem_block) + BLOCK_START + CODEGEN_LINK_ENTRY_OFFSET;
        p->nr++;
    }
    if (p->nr < 2) {
        p->nr = 0;
        return;
    }
    block->poly      = idx + 1;
    /*Its exits are those of every version it may go to*/
    block->flags     = (block->flags & ~(CODEBLOCK_CALLS_C | CODEBLOCK_LINK_STATE)) | hints;
    poly_owner[idx]  = block_nr;
    block->data       = (uint8_t *) codegen_poly_tramp[idx] - BLOCK_START;
    block->page_mask  = 0;
    if (cross)
        block->dirty_mask2 = &poly_dirty_none;
    else
        block->page_mask2 = 0;
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_link_block_changed(block);
#    endif
}
#endif

/*A block cooling down stays dirty and is offered here on each dispatch; if
  its code is rewritten to new bytes each time, no kept version matches and
  the searches only add to the cost of interpreting it. After SWAP_MISS_MAX
  searches in a row without a match its kept versions are not searched for
  SWAP_OFF_TSC; bytes written back as they were are still matched.*/
#define SWAP_MISS_MAX 16
#define SWAP_OFF_TSC  (1ull << 24)

/*A compiled one-chunk byte-mask block whose code bytes were written: if a
  kept version matches them now, it takes the current version's place in
  the block as it stands, which stays on its page's list, rather than the
  block being invalidated and the version put back. The page's dirty bits
  are then dealt with as usual for every other block. Returns 1 if the
  block now holds that version.*/
int
codegen_block_swap_version(codeblock_t *block, page_t *page, uint32_t phys_addr)
{
    const uint32_t block_nr = get_block_nr(block);
    const uint16_t cur      = block_version_cur[block_nr];
    uint16_t      *link     = &block_version_kept[block_nr];

    /*A second chunk only as a byte-mask block's next 64 bytes, as a version
      keeps it (on this page or the next one's translation): the block stays
      in the pages' lists as it is, so every version swapped in has a second
      chunk if the block has*/
    const int      two    = !!block->page_mask2;
    const uint32_t phys_2 = block->phys_2;
    page_t        *page_2 = two ? &pages[phys_2 >> 12] : NULL;

    /*A checked block's versions are compared on entry instead
      (codegen_block_check())*/
    if (!cur || !*link || (block->flags & CODEBLOCK_CHECKED) || block_versions[cur].checked ||
        (block_versions[cur].head_mem_block != block->head_mem_block) ||
        (two && (!(block->flags & CODEBLOCK_BYTE_MASK) || (phys_2 != block_version_phys_2(block)) ||
                 (block_versions[cur].phys_2 != phys_2) || !block_versions[cur].page_mask2)) ||
        (!two && ((block->flags & CODEBLOCK_HAS_PAGE2) || block_versions[cur].page_mask2 || block_versions[cur].family_mask2)))
        return 0;

    /*Bytes written back as they were (a dword store over the opcode that
      changes only the immediate after it): the block's code still stands*/
    if (block_version_chunk_matches(block->phys, block_versions[cur].exec, block->page_mask | block_versions[cur].family_mask, block_versions[cur].code) &&
        (!two || block_version_chunk_matches(phys_2, block_versions[cur].exec_2, block_versions[cur].page_mask2 | block_versions[cur].family_mask2, &block_versions[cur].code[64]))) {
        /*Counted as the version being put back would be*/
        block->swap_misses = 0;
        block_smc_account(block);
        codegen_block_note_written(block);
        block->runs_left = 0;
        check_flush_keep = block;
        codegen_check_flush(page, page->dirty_mask, phys_addr);
        if (two && (page_2 != page))
            codegen_check_flush(page_2, page_2->dirty_mask, phys_2);
        check_flush_keep = NULL;
        return 1;
    }

    if (block->swap_off_until > tsc)
        return 0;

    while (*link) {
        uint16_t         nr = *link;
        block_version_t *v  = &block_versions[nr];

        if (v->checked || (two ? (!v->page_mask2 || (v->phys_2 != phys_2)) : (v->page_mask2 || v->family_mask2)) ||
            (v->cs_sel != CS) || ((v->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) ||
            ((v->status & cpu_cur_status & CPU_STATUS_MASK) != (cpu_cur_status & CPU_STATUS_MASK)) ||
            ((v->flags & CODEBLOCK_STATIC_TOP) && (v->TOP != (cpu_state.TOP & 7))) ||
            ((v->flags & CODEBLOCK_FAMILY_FIXED_CODE) && (block->flags & CODEBLOCK_FAMILY_NOFIX)) ||
            !block_version_chunk_matches(block->phys, v->exec, v->page_mask | v->family_mask, v->code) ||
            (two && !block_version_chunk_matches(phys_2, v->exec_2, v->page_mask2 | v->family_mask2, &v->code[64]))) {
            link = &v->next;
            continue;
        }

        /*The strikes go as invalidate_block() would count them*/
        block->swap_misses = 0;
        block_smc_account(block);
        codegen_block_note_written(block);

        /*v leaves the kept list and the current version takes its place at
          the head, newest first, so the list keeps its length*/
        *link                        = v->next;
        v->next                      = 0;
        block_version_set_state(v, BLOCK_VERSION_CURRENT);
        block_version_set_state(&block_versions[cur], BLOCK_VERSION_KEPT);
        block_versions[cur].next     = block_version_kept[block_nr];
        block_version_kept[block_nr] = cur;
        block_version_cur[block_nr]  = nr;

        block->head_mem_block = v->head_mem_block;
        block->data           = codeblock_allocator_get_ptr(v->head_mem_block);
        block->page_mask      = v->page_mask;
        block->page_mask2     = v->page_mask2;
        block->status         = v->status;
        block->TOP            = v->TOP;
        block->flags          = (block->flags & ~(CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP | CODEBLOCK_FAMILY_FIXED_CODE | CODEBLOCK_CALLS_C | CODEBLOCK_LINK_STATE)) | v->flags | CODEBLOCK_WAS_RECOMPILED;
        block->runs_left      = 0;
        page->byte_code_present_mask[(block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK] |= block->page_mask;
        if (two)
            page_2->byte_code_present_mask[(phys_2 >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK] |= block->page_mask2;
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
        codegen_link_block_changed(block);
#endif
        check_flush_keep = block;
        codegen_check_flush(page, page->dirty_mask, phys_addr);
        if (two && (page_2 != page))
            codegen_check_flush(page_2, page_2->dirty_mask, phys_2);
        check_flush_keep = NULL;
#ifdef CODEGEN_BACKEND_HAS_POLY
        if (block->smc_swaps < 255)
            block->smc_swaps++;
        if ((block->smc_swaps >= 8) && !block->poly)
            poly_enter(block);
#endif
        return 1;
    }
    if (++block->swap_misses >= SWAP_MISS_MAX) {
        block->swap_misses    = 0;
        block->swap_off_until = tsc + SWAP_OFF_TSC;
    }
    return 0;
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

        block_version_set_state(&block_versions[cur], BLOCK_VERSION_KEPT);
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
            ((v->flags & CODEBLOCK_FAMILY_FIXED_CODE) && (block->flags & CODEBLOCK_FAMILY_NOFIX)) ||
            !block_version_chunk_matches(block->phys, v->exec, v->page_mask | v->family_mask, v->code)) {
            link = &v->next;
            continue;
        }
        if ((v->page_mask2 || v->family_mask2) && ((v->phys_2 != block_version_phys_2(block)) ||
                              !block_version_chunk_matches(v->phys_2, v->exec_2, v->page_mask2 | v->family_mask2, &v->code[64]))) {
            link = &v->next;
            continue;
        }

        *link                       = v->next;
        v->next                     = 0;
        block_version_set_state(v, BLOCK_VERSION_CURRENT);
        block_version_cur[block_nr] = nr;

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
        block->flags          = (block->flags & ~(CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP | CODEBLOCK_FAMILY_FIXED_CODE | CODEBLOCK_CALLS_C | CODEBLOCK_LINK_STATE)) | v->flags | CODEBLOCK_WAS_RECOMPILED;
        block->runs_left      = 0;
        /*A miss the code it held set is not this version's*/
        block->family_miss    = 0;
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

    if (block->runs_left) {
        if (block->smc_strikes < SMC_THRASH_MAX_STRIKES)
            block->smc_strikes++;
        if (block->smc_strikes >= SMC_THRASH_STRIKE_THRESHOLD)
            cooldown = tsc + ((uint64_t) SMC_THRASH_BASE_COOLDOWN << (block->smc_strikes - SMC_THRASH_STRIKE_THRESHOLD));
    } else
        block->smc_strikes = 0;
    if (block == block_checked_entering) {
        block_checked_cooldown_block = block;
        block_checked_cooldown       = cooldown;
    } else
        block->smc_cooldown_until = cooldown;
    block_checked_drop_code(block);
}

/*A checked block left without code: off its page lists, as
  invalidate_block() takes a block off them (the put back or the compile
  lists it again), its current version kept*/
static void
block_checked_drop_code(codeblock_t *block)
{
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
    if (offset >= 128) {
        block_version_start_ok = 0;
        return;
    }
    if ((addr >> 12) == pccache)
        fetch = (const uint8_t *) ((((uintptr_t) &pccache2[addr]) & 0x00000000ffffffffULL) | (((uintptr_t) &pccache2[0]) & 0xffffffff00000000ULL));
    else {
        /*One crossing into the next page leaves the fetch cache there*/
        const uint32_t phys = get_phys_noabrt(addr);

        if ((phys == 0xffffffff) || !_mem_exec[phys >> MEM_GRANULARITY_BITS]) {
            block_version_start_ok = 0;
            return;
        }
        fetch = &_mem_exec[phys >> MEM_GRANULARITY_BITS][phys & MEM_GRANULARITY_MASK];
    }
    {
        /*Each chunk through the memory it was read from*/
        const uint8_t *chunk_1 = &block_version_start_exec[(block->phys & MEM_GRANULARITY_MASK) & ~0x3f];
        const uint8_t *chunk_2 = &block_version_start_exec_2[(block_version_start_phys_2 & MEM_GRANULARITY_MASK) & ~0x3f];

        if (fetch != ((offset < 64) ? &chunk_1[offset] : &chunk_2[offset - 64])) {
            block_version_start_ok = 0;
            return;
        }
        /*To the end of the snapshot: the decoders take any number of
          prefixes, so an instruction is not bounded at 15 bytes here*/
        if (((offset < 64) && memcmp(&chunk_1[offset], &block_version_start_code[offset], 64 - offset)) ||
            memcmp(&chunk_2[(offset < 64) ? 0 : (offset - 64)], &block_version_start_code[(offset < 64) ? 64 : offset], (offset < 64) ? 64 : (128 - offset))) {
            block_version_start_ok = 0;
            return;
        }
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

uint32_t codegen_link_epoch = 1;

/*The epoch came round past 0: a record that passed at an epoch now to come
  again would pass then without its checks, so every record starts over
  at 0, which none passes*/
void
codegen_link_epoch_wrap(void)
{
    codegen_link_epoch = 1;
#if defined(CODEGEN_BACKEND_HAS_BLOCK_LINK) && (defined(__aarch64__) || defined(_M_ARM64))
    for (int b = 0; b < BLOCK_SIZE; b++) {
        for (int n = 0; n < CODEGEN_LINK_SLOTS; n++)
            codegen_link_slots[b][n].epoch = 0;
    }
#    ifdef CODEGEN_BACKEND_HAS_LINK_SETS
    for (int c = 0; c < CODEGEN_LINK_SETS; c++) {
        for (int n = 0; n < 3; n++)
            codegen_link_sets[c].e[n].epoch = 0;
    }
#    endif
#    ifdef CODEGEN_BACKEND_HAS_LINK_TABLE
    for (int c = 0; c < CODEGEN_LINK_TABLE_SIZE; c++)
        codegen_link_table[c].epoch = 0;
#    endif
#endif
}

#if defined(CODEGEN_BACKEND_HAS_BLOCK_LINK) && (defined(__aarch64__) || defined(_M_ARM64))
/*The state the arm64 link routine's full way last compared a record with:
  cpu_cur_status | npxc & 0x0f00 << 16, and cr0 & 0xc0000000 | CPL. With no
  C run since then (link_c_ran clear) it is the state now, so the short way
  compares records with these rather than working it out*/
uint32_t codegen_link_state[2] __attribute__((aligned(8)));
#endif

void
codegen_flush(void)
{
    codegen_link_epoch_bump();
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
#if defined(__aarch64__) || defined(_M_ARM64)
    uop_FETCH_WALK(ir, addr);
#else
    uop_CALL_INSTRUCTION_FUNC(ir, dynarec_fetch_walk, addr);
#endif
}

void
codegen_mark_code_present_multibyte(codeblock_t *block, uint32_t start_pc, int len)
{
    if (len && (block->flags & CODEBLOCK_BYTE_MASK)) {
        /*A byte-mask block marks its first two chunks only: past them it
          holds only bytes read as it runs (codegen_family_beyond_ok())*/
        const uint32_t base = block->pc & ~0x3f;

        if ((start_pc - base) >= 128)
            return;
        if (((start_pc + (len - 1)) - base) >= 128)
            len = (int) ((base + 128) - start_pc);
    }
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

            if (((start_pc ^ block->pc) & ~0xfff) && !((end_pc ^ block->pc) & ~0xfff)) /*Crosses from a second page just below the first*/
            {
                for (; start_pc_shifted <= 63; start_pc_shifted++)
                    block->page_mask2 |= ((uint64_t) 1 << start_pc_shifted);
                for (start_pc_shifted = 0; start_pc_shifted <= end_pc_shifted; start_pc_shifted++)
                    block->page_mask |= ((uint64_t) 1 << start_pc_shifted);
            } else if ((start_pc ^ block->pc) & ~0xfff) /*Starts in second page*/
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
