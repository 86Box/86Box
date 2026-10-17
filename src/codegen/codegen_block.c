#include <inttypes.h>
#include <stddef.h>
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

#ifdef CODEGEN_CHAIN
/* Chains of linked blocks: the generation every link carries, moved on
   whenever a link could become wrong (a block's code changes or goes, the
   TLB is flushed). */
uint64_t codegen_chain_gen = 1; /* 64 bits: never wraps back onto a stale stub */

/* The indirect link table (codegen.h); and the flag an
   indirect exit sets when the PC it left for was not there. */
codegen_ind_t codegen_ind_cache[CODEGEN_IND_SIZE];
uint64_t      codegen_link_unlinked = UINT64_MAX;
_Static_assert(sizeof(codegen_ind_t) == 64, "the indirect stub indexes 64-byte entries");
_Static_assert((offsetof(codegen_ind_t, gen) == 16) && (offsetof(codegen_ind_t, target) == 24) &&
                   (offsetof(codegen_ind_t, link_gen) == 32) && (offsetof(codegen_ind_t, group_word) == 40) &&
                   (offsetof(codegen_ind_t, group_value) == 48),
               "the indirect stub reads these fields at fixed offsets");

/* The blocks a link or an indirect entry may reach: set as a link is made
   (codegen_links_note), cleared by the dispatcher's side alone. A set bit
   with no link behind it only costs a look. */
static uint64_t codegen_link_targets[BLOCK_SIZE / 64];

/* The linked blocks, grouped by the linear page they start on, whether
   they run onto the next, and what the page (or both) translated to when
   they were linked (link_pte, link_pte2): a revalidation walks each
   group's pages once and looks at its blocks only when a page moved, so
   it costs the linked pages, not the linked blocks. A block whose pages
   did not translate to its code, or one with no group to spare, is
   checked alone (the solo list). Kept with the target bits.

   A group whose pages stop translating as they did -- another process's
   pages once CR3 moves on, most often simply not there -- is held, not
   dropped: a link into it compares the group's word as well as its
   block's generation (codegen_lg_word), and holding it sets bit 63 there,
   one write for all its links. A held group leaves the passes, and comes
   back when a link is next made to one of its blocks with its pages
   translating as they did (codegen_lg_find): every link into it holds
   again, none made over. Held groups are taken back, oldest first, when
   no group is free. */
#define CODEGEN_LG_MAX  65536
#define CODEGEN_LG_HASH 16384
#define CODEGEN_LG_SOLO (-2)
#define CODEGEN_LG_HELD (1ULL << 63)
#define CODEGEN_LG_HASH_OF(lin) ((((lin) >> 12) ^ ((lin) >> 24)) & (CODEGEN_LG_HASH - 1))
typedef struct codegen_lg_t {
    uint32_t lin;
    int32_t  spans; /* its blocks run onto lin + 4K */
    uint64_t umask; /* ~0, or ~2 where no block was linked at CPL 3 */
    uint64_t pte;
    uint64_t pte2;  /* lin + 4K's, when spans */
    int32_t  head;   /* first block, -1 for none */
    uint32_t count;
    int32_t  hnext;  /* next group in its hash chain, or on the free list */
    int32_t  active; /* its place in codegen_lg_active, -1 while held */
    int32_t  held_prev, held_next; /* its neighbours among the held, oldest first */
} codegen_lg_t;
static codegen_lg_t codegen_lg[CODEGEN_LG_MAX];
/* What a link into the group compares (the stub's second test): a number
   never given out before while the group holds, with CODEGEN_LG_HELD while
   it is held, 0 once it is released. A solo block's links compare
   codegen_lg_solo_word, always 0: its own generation holds them. */
static uint64_t     codegen_lg_word[CODEGEN_LG_MAX];
static uint64_t     codegen_lg_seq = 1;
uint64_t            codegen_lg_solo_word;
static int32_t      codegen_lg_held_head = -1;
static int32_t      codegen_lg_held_tail = -1;
static int32_t      codegen_lg_hash[CODEGEN_LG_HASH];
static int32_t      codegen_lg_active[CODEGEN_LG_MAX];
static int          codegen_lg_active_n;
static int32_t      codegen_lg_free;
static int32_t      codegen_lg_solo;

static void codegen_lp_init(void);

static void
codegen_lg_init(void)
{
    for (int h = 0; h < CODEGEN_LG_HASH; h++)
        codegen_lg_hash[h] = -1;
    for (int g = 0; g < CODEGEN_LG_MAX; g++)
        codegen_lg[g].hnext = (g + 1 < CODEGEN_LG_MAX) ? (g + 1) : -1;
    for (int g = 0; g < CODEGEN_LG_MAX; g++)
        codegen_lg_word[g] = 0;
    codegen_lp_init();
    codegen_lg_free      = 0;
    codegen_lg_active_n  = 0;
    codegen_lg_solo      = -1;
    codegen_lg_held_head = -1;
    codegen_lg_held_tail = -1;
}

static void
codegen_lg_active_add(int32_t g)
{
    codegen_lg[g].active                     = codegen_lg_active_n;
    codegen_lg_active[codegen_lg_active_n++] = g;
}

/* Out of the active array: the last takes its place. */
static void
codegen_lg_active_remove(int32_t g)
{
    const int32_t last = codegen_lg_active[--codegen_lg_active_n];

    codegen_lg_active[codegen_lg[g].active] = last;
    codegen_lg[last].active                 = codegen_lg[g].active;
    codegen_lg[g].active                    = -1;
}

static void
codegen_lg_held_remove(int32_t g)
{
    const int32_t prev = codegen_lg[g].held_prev;
    const int32_t next = codegen_lg[g].held_next;

    if (prev >= 0)
        codegen_lg[prev].held_next = next;
    else
        codegen_lg_held_head = next;
    if (next >= 0)
        codegen_lg[next].held_prev = prev;
    else
        codegen_lg_held_tail = prev;
}

/* Its pages no longer translate as its links were made under. */
static void
codegen_lg_hold(int32_t g)
{
    codegen_lg_active_remove(g);
    __atomic_fetch_or(&codegen_lg_word[g], CODEGEN_LG_HELD, __ATOMIC_SEQ_CST);
    codegen_lg[g].held_prev = codegen_lg_held_tail;
    codegen_lg[g].held_next = -1;
    if (codegen_lg_held_tail >= 0)
        codegen_lg[codegen_lg_held_tail].held_next = g;
    else
        codegen_lg_held_head = g;
    codegen_lg_held_tail = g;
}

/* They do again. */
static void
codegen_lg_resume(int32_t g)
{
    codegen_lg_held_remove(g);
    __atomic_fetch_and(&codegen_lg_word[g], ~CODEGEN_LG_HELD, __ATOMIC_SEQ_CST);
    codegen_lg_active_add(g);
}

static void codegen_link_bit_clear(int nr);

/* No group free: the one held longest lets its blocks' links go, and is
   released as the last of them leaves. No code is freed, so a link the
   dispatcher has pending stands. */
static void
codegen_lg_reclaim(void)
{
    const int32_t g = codegen_lg_held_head;

    for (int32_t nr = codegen_lg[g].head; nr >= 0;) {
        const int32_t next = codeblock[nr].link_gnext;

        __atomic_fetch_add(&codeblock[nr].link_gen, 1, __ATOMIC_SEQ_CST);
        codegen_link_bit_clear(nr);
        codeblock[nr].link_cpl3 = 0; /* no link left */
        nr = next;
    }
}

/* What a link to the block compares besides its generation, and where:
   read after codegen_links_note has put it in its group. */
uint64_t *
codegen_link_group_word(const codeblock_t *block, uint64_t *value)
{
    const int32_t g = block->link_group;

    if (g < 0) {
        *value = 0;
        return &codegen_lg_solo_word;
    }
    *value = codegen_lg_word[g];
    return &codegen_lg_word[g];
}

static void
codegen_lg_list_add(int32_t *head, int nr)
{
    codeblock[nr].link_gnext = *head;
    codeblock[nr].link_gprev = -1;
    if (*head >= 0)
        codeblock[*head].link_gprev = nr;
    *head = nr;
}

static void
codegen_lg_list_remove(int32_t *head, int nr)
{
    const int32_t prev = codeblock[nr].link_gprev;
    const int32_t next = codeblock[nr].link_gnext;

    if (prev >= 0)
        codeblock[prev].link_gnext = next;
    else
        *head = next;
    if (next >= 0)
        codeblock[next].link_gprev = prev;
}

static int32_t
codegen_lg_find(uint32_t lin, int spans, uint64_t umask, uint64_t pte, uint64_t pte2)
{
    const int h = CODEGEN_LG_HASH_OF(lin);
    int32_t   g;

    for (g = codegen_lg_hash[h]; g >= 0; g = codegen_lg[g].hnext) {
        if ((codegen_lg[g].lin == lin) && (codegen_lg[g].spans == spans) && (codegen_lg[g].umask == umask) && (codegen_lg[g].pte == pte) &&
            (!spans || (codegen_lg[g].pte2 == pte2))) {
            /* Its pages translate as they did (the caller has just walked
               them): the links it held are good again. */
            if (codegen_lg[g].active < 0)
                codegen_lg_resume(g);
            return g;
        }
    }
    if ((codegen_lg_free < 0) && (codegen_lg_held_head >= 0))
        codegen_lg_reclaim();
    if (codegen_lg_free < 0) {
        return -1;
    }
    g               = codegen_lg_free;
    codegen_lg_free = codegen_lg[g].hnext;

    codegen_lg[g].lin    = lin;
    codegen_lg[g].spans  = spans;
    codegen_lg[g].umask  = umask;
    codegen_lg[g].pte    = pte;
    codegen_lg[g].pte2   = spans ? pte2 : 0;
    codegen_lg[g].head   = -1;
    codegen_lg[g].count  = 0;
    codegen_lg[g].hnext  = codegen_lg_hash[h];
    codegen_lg_hash[h]   = g;
    __atomic_store_n(&codegen_lg_word[g], codegen_lg_seq++, __ATOMIC_SEQ_CST);
    codegen_lg_active_add(g);
    return g;
}

static void
codegen_lg_release(int32_t g)
{
    const int h    = CODEGEN_LG_HASH_OF(codegen_lg[g].lin);
    int32_t  *link = &codegen_lg_hash[h];

    while (*link != g)
        link = &codegen_lg[*link].hnext;
    *link = codegen_lg[g].hnext;

    if (codegen_lg[g].active < 0)
        codegen_lg_held_remove(g);
    else
        codegen_lg_active_remove(g);
    __atomic_store_n(&codegen_lg_word[g], 0, __ATOMIC_SEQ_CST); /* no link into it holds again */

    codegen_lg[g].hnext = codegen_lg_free;
    codegen_lg_free     = g;
}

/* How many linked blocks have code on each physical page (both pages of a
   block running onto a second): writes to a page with none need no signal,
   since only a link skips the dirty test a dispatch makes. Kept with the
   target bits. */
#define CODEGEN_LINK_PHYS_PAGES (1u << 20)
static uint32_t codegen_link_phys_count[CODEGEN_LINK_PHYS_PAGES];

/* And which they are: a list for each physical page, of nodes nr * 2 +
   page (0 for a block's first page, 1 for its second). A write to a page
   concerns only its linked blocks -- a dispatch tests any other's dirty
   bits -- and a driver's page can hold hundreds of blocks of which a few
   are linked, walked once for every write that reached the page. */
static int32_t codegen_lp_head[CODEGEN_LINK_PHYS_PAGES];

static void
codegen_lp_init(void)
{
    memset(codegen_lp_head, 0xff, sizeof(codegen_lp_head));
}

static inline uint32_t
codegen_lp_page(uint32_t phys)
{
    return (phys >> 12) & (CODEGEN_LINK_PHYS_PAGES - 1);
}

static void
codegen_lp_insert(int32_t node, uint32_t pg)
{
    codeblock_t *b = &codeblock[node >> 1];
    const int    k = node & 1;

    b->lp_prev[k] = -1;
    b->lp_next[k] = codegen_lp_head[pg];
    if (codegen_lp_head[pg] >= 0)
        codeblock[codegen_lp_head[pg] >> 1].lp_prev[codegen_lp_head[pg] & 1] = node;
    codegen_lp_head[pg] = node;
}

static void
codegen_lp_remove(int32_t node, uint32_t pg)
{
    const codeblock_t *b    = &codeblock[node >> 1];
    const int          k    = node & 1;
    const int32_t      prev = b->lp_prev[k];
    const int32_t      next = b->lp_next[k];

    if (prev >= 0)
        codeblock[prev >> 1].lp_next[prev & 1] = next;
    else
        codegen_lp_head[pg] = next;
    if (next >= 0)
        codeblock[next >> 1].lp_prev[next & 1] = prev;
}

static inline void
codegen_link_phys_add(const codeblock_t *block, int d)
{
    const int32_t nr = get_block_nr((codeblock_t *) block);

    codegen_link_phys_count[codegen_lp_page(block->link_phys)] += d;
    if (d > 0)
        codegen_lp_insert(nr * 2, codegen_lp_page(block->link_phys));
    else
        codegen_lp_remove(nr * 2, codegen_lp_page(block->link_phys));
    if (block->link_phys_2 != 0xffffffff) {
        codegen_link_phys_count[codegen_lp_page(block->link_phys_2)] += d;
        if (d > 0)
            codegen_lp_insert(nr * 2 + 1, codegen_lp_page(block->link_phys_2));
        else
            codegen_lp_remove(nr * 2 + 1, codegen_lp_page(block->link_phys_2));
    }
}

/* Whether a block's code runs onto the next 4K page, which its links must
   then translate too. For a byte-masked block page_mask2 is its next
   64-byte chunk (codegen_mark_code_present), usually on its own page. */
static inline int
codegen_link_spans(const codeblock_t *block)
{
    if (!block->page_mask2)
        return 0;
    if (block->flags & CODEBLOCK_BYTE_MASK)
        return ((block->phys_2 ^ block->phys) & ~0xfff) != 0;
    return 1;
}

static void
codegen_link_bit_set(codeblock_t *block, int nr)
{
    const uint64_t bit = 1ULL << (nr & 63);
    const uint32_t lin = block->pc & ~0xfff;
    int32_t        g   = -1;

    if (__atomic_fetch_or(&codegen_link_targets[nr >> 6], bit, __ATOMIC_SEQ_CST) & bit)
        return;
    block->link_phys   = block->phys;
    block->link_phys_2 = block->page_mask2 ? block->phys_2 : 0xffffffff;
    codegen_link_phys_add(block, 1);
    /* Grouped only where its pages translated to its code, as a check
       compares the group's walk with what its blocks were linked under. */
    if ((block->link_pte & 1) && ((block->link_pte & ~0xfffULL) == (block->phys & ~0xfff)) &&
        (!codegen_link_spans(block) || ((block->link_pte2 & 1) && ((block->link_pte2 & ~0xfffULL) == (block->phys_2 & ~0xfff)))))
        g = codegen_lg_find(lin, codegen_link_spans(block), block->link_cpl3 ? ~0ULL : ~2ULL, block->link_pte, block->link_pte2);
    if (g < 0) {
        block->link_group = CODEGEN_LG_SOLO;
        codegen_lg_list_add(&codegen_lg_solo, nr);
    } else {
        block->link_group = g;
        codegen_lg_list_add(&codegen_lg[g].head, nr);
        codegen_lg[g].count++;
    }
}

static void
codegen_link_bit_clear(int nr)
{
    const uint64_t bit = 1ULL << (nr & 63);
    int32_t        g;

    if (!(__atomic_fetch_and(&codegen_link_targets[nr >> 6], ~bit, __ATOMIC_SEQ_CST) & bit))
        return;
    codegen_link_phys_add(&codeblock[nr], -1);
    g = codeblock[nr].link_group;
    if (g == CODEGEN_LG_SOLO)
        codegen_lg_list_remove(&codegen_lg_solo, nr);
    else {
        codegen_lg_list_remove(&codegen_lg[g].head, nr);
        if (--codegen_lg[g].count == 0)
            codegen_lg_release(g);
    }
}

/* Translations flushed since the loop last looked: all of them,
   or these linear pages (INVLPG). */
#define CODEGEN_REVAL_PAGES 8
static int      codegen_reval_all;
static int      codegen_reval_count;
static uint32_t codegen_reval_pages[CODEGEN_REVAL_PAGES];

static void
codegen_ind_reset(void)
{
    for (int c = 0; c < CODEGEN_IND_SIZE; c++) {
        codegen_ind_cache[c].pc          = 0;
        codegen_ind_cache[c].gen         = 0;
        codegen_ind_cache[c].link_gen    = &codegen_link_unlinked;
        codegen_ind_cache[c].group_word  = &codegen_lg_solo_word;
        codegen_ind_cache[c].group_value = 0;
    }
}

void
codegen_links_note(codeblock_t *block)
{
    const int      nr  = get_block_nr(block);
    const uint32_t pg  = block->pc & ~0xfff;
    const int32_t  g   = block->link_group;
    uint64_t       pte;
    uint64_t       pte2;

    /* A page's user bit matters only to links made at CPL 3 (the key keeps
       them apart); once one is, it matters until the block's links all go.
       Paging turned on and off -- a boot loader's BIOS calls -- makes it
       flip for supervisor code, which otherwise loses every link. */
    if ((cpu_state.seg_cs.access & 0x60) == 0x60)
        block->link_cpl3 = 1;
    pte         = mmu_link_translate(pg);
    pte2        = codegen_link_spans(block) ? mmu_link_translate(pg + 0x1000) : 0;
    if (!block->link_cpl3) {
        pte &= ~2ULL;
        pte2 &= ~2ULL;
    }

    /* Already in the group its pages translate to now: it stays, and the
       links into the group made before hold (a group left empty would be
       released, and they with it). A held group is good again. */
    if ((__atomic_load_n(&codegen_link_targets[nr >> 6], __ATOMIC_SEQ_CST) & (1ULL << (nr & 63))) && (g >= 0) &&
        (codegen_lg[g].lin == pg) && (codegen_lg[g].spans == codegen_link_spans(block)) &&
        (codegen_lg[g].umask == (block->link_cpl3 ? ~0ULL : ~2ULL)) && (codegen_lg[g].pte == pte) &&
        (!codegen_link_spans(block) || (codegen_lg[g].pte2 == pte2))) {
        block->link_pte  = pte;
        block->link_pte2 = pte2;
        if (codegen_lg[g].active < 0)
            codegen_lg_resume(g);
        return;
    }

    /* Otherwise out of its group first: it goes where its page translates
       now. */
    codegen_link_bit_clear(nr);
    block->link_pte  = pte;
    block->link_pte2 = pte2;
    codegen_link_bit_set(block, nr);
}

/* From anywhere: the block's links miss from now on. A link the dispatcher
   has pending to it is safe: it read the generation before it found the
   code clean. */
void
codegen_links_drop(codeblock_t *block)
{
    __atomic_fetch_add(&block->link_gen, 1, __ATOMIC_SEQ_CST);
}

/* The dispatcher's side, as a block goes or is compiled again: the block has
   no link left, so its bit goes too, and the stub the dispatcher has
   pending may be in the code freed, so that link is not made. */
static void
codegen_links_drop_owned(codeblock_t *block)
{
    const int nr = get_block_nr(block);

    codegen_links_drop(block);
    codegen_link_bit_clear(nr);
    block->link_cpl3 = 0; /* no link left */
    __atomic_fetch_add(&codegen_chain_gen, 1, __ATOMIC_SEQ_CST);
}

void
codegen_links_drop_all(int owner)
{
    for (int w = 0; w < (BLOCK_SIZE / 64); w++) {
        uint64_t bits = __atomic_load_n(&codegen_link_targets[w], __ATOMIC_SEQ_CST);

        while (bits) {
            const int b = __builtin_ctzll(bits);

            bits &= bits - 1;
            __atomic_fetch_add(&codeblock[(w << 6) | b].link_gen, 1, __ATOMIC_SEQ_CST);
            if (owner) {
                codegen_link_bit_clear((w << 6) | b);
                codeblock[(w << 6) | b].link_cpl3 = 0; /* no link left */
            }
        }
    }
    __atomic_fetch_add(&codegen_chain_gen, 1, __ATOMIC_SEQ_CST);
}

void
codegen_links_revalidate_request(int all, uint32_t lin)
{
    if (all || (codegen_reval_count >= CODEGEN_REVAL_PAGES))
        codegen_reval_all = 1;
    else
        codegen_reval_pages[codegen_reval_count++] = lin & ~0xfff;
    /* The chain leaves at its next link, and the loop looks before the
       next dispatch, the flushing instruction done. */
    mem_chain_code(MEM_CHAIN_CODE_REVALIDATE);
}

/* One revalidation's page walks, by linear page: many linked blocks share
   a page, and each page is walked once a pass. An entry holds only for the
   pass that filled it. */
#define CODEGEN_REVAL_CACHE 2048
static struct {
    uint64_t pass;
    uint32_t page;
    uint64_t pte;
} codegen_reval_cache[CODEGEN_REVAL_CACHE];
static uint64_t codegen_reval_pass; /* 64 bits: never wraps back onto a stale entry */

static uint64_t
codegen_reval_translate(uint32_t pg)
{
    const uint32_t h = ((pg >> 12) ^ (pg >> 23)) & (CODEGEN_REVAL_CACHE - 1);

    if ((codegen_reval_cache[h].pass == codegen_reval_pass) && (codegen_reval_cache[h].page == pg))
        return codegen_reval_cache[h].pte;
    codegen_reval_cache[h].pass = codegen_reval_pass;
    codegen_reval_cache[h].page = pg;
    codegen_reval_cache[h].pte  = mmu_link_translate(pg);
    return codegen_reval_cache[h].pte;
}

/* Whether a linked block's pages still translate where it was compiled
   from, with the user/supervisor bit the links were made under. */
static int
codegen_links_translation_holds(const codeblock_t *block)
{
    const uint32_t pg = block->pc & ~0xfff;
    uint64_t       t  = codegen_reval_translate(pg);

    const uint64_t u = block->link_cpl3 ? 2 : 0; /* the user bit, where it matters */

    if (!(t & 1) || ((t & ~0xfffULL) != (block->phys & ~0xfff)) || ((t ^ block->link_pte) & u))
        return 0;
    if (codegen_link_spans(block)) {
        t = codegen_reval_translate(pg + 0x1000);
        if (!(t & 1) || ((t & ~0xfffULL) != (block->phys_2 & ~0xfff)) || ((t ^ block->link_pte2) & u))
            return 0;
    }
    return 1;
}

/* A group's page (or either page) moved: every block in it loses its
   links. */
static void
codegen_lg_check(int32_t g)
{
    if (((codegen_reval_translate(codegen_lg[g].lin) & codegen_lg[g].umask) == codegen_lg[g].pte) &&
        (!codegen_lg[g].spans || ((codegen_reval_translate(codegen_lg[g].lin + 0x1000) & codegen_lg[g].umask) == codegen_lg[g].pte2)))
        return;
    codegen_lg_hold(g);
}

static int
codegen_reval_wants(int all, int count, uint32_t first, int two_pages)
{
    if (all)
        return 1;
    for (int c = 0; c < count; c++) {
        if ((codegen_reval_pages[c] == first) || (two_pages && (codegen_reval_pages[c] == (first + 0x1000))))
            return 1;
    }
    return 0;
}

/* The loop's look after a translation flush (CR3, CR0 and
   CR4 loads, INVLPG, task switches): only the links whose target no longer
   translates to its code go. An operating system's kernel keeps its
   mappings in every address space, so a context switch leaves them. */
void
codegen_links_revalidate(void)
{
    const int all   = codegen_reval_all;
    const int count = codegen_reval_count;

    codegen_reval_all   = 0;
    codegen_reval_count = 0;

    /* Paging off at the last pass and still off, the same A20: no
       translation can have changed (real mode, and a boot loader's switches
       into protected mode and back without paging). */
    {
        static int      last_off;
        static uint32_t last_rammask;
        const int       off = !(cr0 & 0x80000000);

        if (off && last_off && (rammask == last_rammask))
            return;
        last_off     = off;
        last_rammask = rammask;
    }

    /* A new pass: no walk of an earlier one is trusted. Pass 0 is never
       used, so an empty entry never matches. */
    if (++codegen_reval_pass == 0)
        codegen_reval_pass = 1;

    if (all) {
        /* A released group takes the last place: going down, that one has
           been looked at already. */
        for (int i = codegen_lg_active_n - 1; i >= 0; i--) {
            if (i < codegen_lg_active_n)
                codegen_lg_check(codegen_lg_active[i]);
        }
    } else {
        /* The groups starting on each page, and those running onto it
           from the page before. */
        for (int c = 0; c < count; c++) {
            const uint32_t pg = codegen_reval_pages[c];
            int32_t        g  = codegen_lg_hash[CODEGEN_LG_HASH_OF(pg)];

            while (g >= 0) {
                const int32_t next = codegen_lg[g].hnext;

                if ((codegen_lg[g].lin == pg) && (codegen_lg[g].active >= 0))
                    codegen_lg_check(g);
                g = next;
            }
            g = codegen_lg_hash[CODEGEN_LG_HASH_OF(pg - 0x1000)];
            while (g >= 0) {
                const int32_t next = codegen_lg[g].hnext;

                if (codegen_lg[g].spans && (codegen_lg[g].lin == (pg - 0x1000)) && (codegen_lg[g].active >= 0))
                    codegen_lg_check(g);
                g = next;
            }
        }
    }

    /* The blocks checked alone. */
    for (int32_t nr = codegen_lg_solo; nr >= 0;) {
        codeblock_t  *block = &codeblock[nr];
        const int32_t next  = block->link_gnext;

        if (codegen_reval_wants(all, count, block->pc & ~0xfff, codegen_link_spans(block))) {
            if (!block->valid || !block->head_mem_block || !codegen_links_translation_holds(block)) {
                codegen_links_drop_owned(block);
            }
        }
        nr = next;
    }
}

uint32_t codegen_ind_miss;

static inline uint32_t
codegen_ind_hash(uint32_t pc)
{
    return (pc ^ (pc >> 12)) & (CODEGEN_IND_SIZE - 1);
}

/* The dispatcher found `block` valid where an indirect exit missed: the
   exits reach it straight from now on, for as long as the generation, CS
   base and status hold. */
void
codegen_ind_fill(codeblock_t *block, uint64_t gen)
{
    codegen_ind_t *e = &codegen_ind_cache[codegen_ind_hash(block->pc)];

    codegen_links_note(block);
    e->pc          = block->pc;
    e->cs_base     = block->_cs;
    e->status      = CODEGEN_CHAIN_KEY();
    e->gen         = gen;
    e->target      = &block->data[block->chain_entry];
    e->link_gen    = &block->link_gen;
    e->group_word  = codegen_link_group_word(block, &e->group_value);
}

uint8_t *codegen_chain_last_exit;

/* The physical pages compiled code of linked blocks was written on, noted
   by the writer (mem.c) and taken at the loop's next look
   (codegen_links_written). */
#define CODEGEN_WRITTEN_PAGES (1u << 20) /* 4 GB of 4K pages */
static uint64_t codegen_written[CODEGEN_WRITTEN_PAGES / 64];
static uint64_t codegen_written_sum[CODEGEN_WRITTEN_PAGES / 64 / 64];

/* Whether a block's code is as compiled, as the dispatcher tests it. */
static inline int
codegen_block_code_clean(const codeblock_t *b)
{
    if (b->page_mask & __atomic_load_n(b->dirty_mask, __ATOMIC_SEQ_CST))
        return 0;
    if (b->page_mask2 && b->dirty_mask2 && (b->page_mask2 & __atomic_load_n(b->dirty_mask2, __ATOMIC_SEQ_CST)))
        return 0;
    return 1;
}

static void
codegen_written_note(const page_t *p)
{
    const uint32_t idx = (uint32_t) (p - pages);

    if (idx >= CODEGEN_WRITTEN_PAGES) {
        __atomic_store_n(&codegen_written_sum[0], ~0ULL, __ATOMIC_SEQ_CST); /* cannot happen: pages_sz is smaller */
        return;
    }
    __atomic_fetch_or(&codegen_written[idx >> 6], 1ULL << (idx & 63), __ATOMIC_SEQ_CST);
    __atomic_fetch_or(&codegen_written_sum[idx >> 12], 1ULL << ((idx >> 6) & 63), __ATOMIC_SEQ_CST);
}

/* The loop's look after compiled code was written: on each page written,
   the blocks whose code is dirty lose their links. One walk a page however
   many writes it took; the writer only noted the page. A block whose dirty
   bits a dispatch has taken since was invalidated there, its links with it
   (codegen_check_flush). */
void
codegen_links_written(void)
{
    for (uint32_t s = 0; s < (CODEGEN_WRITTEN_PAGES / 64 / 64); s++) {
        uint64_t sum;

        /* Read first: most summary words are empty, and an exchange is a
           locked write each. */
        if (!__atomic_load_n(&codegen_written_sum[s], __ATOMIC_RELAXED))
            continue;
        sum = __atomic_exchange_n(&codegen_written_sum[s], 0, __ATOMIC_SEQ_CST);

        while (sum) {
            const uint32_t w    = (s << 6) | __builtin_ctzll(sum);
            uint64_t       bits = __atomic_exchange_n(&codegen_written[w], 0, __ATOMIC_SEQ_CST);

            sum &= sum - 1;
            while (bits) {
                const uint32_t idx = (w << 6) | __builtin_ctzll(bits);

                bits &= bits - 1;
                if (idx >= pages_sz)
                    continue;
                /* The page's linked blocks: a dropped block leaves the list,
                   so the next is taken first, past any node of the same
                   block (both its pages in one list). */
                for (int32_t node = codegen_lp_head[codegen_lp_page(idx << 12)]; node >= 0;) {
                    codeblock_t *b    = &codeblock[node >> 1];
                    int32_t      next = b->lp_next[node & 1];

                    while ((next >= 0) && ((next >> 1) == (node >> 1)))
                        next = codeblock[next >> 1].lp_next[next & 1];
                    if (!codegen_block_code_clean(b))
                        codegen_links_drop_owned(b);
                    node = next;
                }
            }
        }
    }
}

/* Whether a write with these masks (mem.c) reaches a block's code. */
static int
codegen_page_block_hits(const codeblock_t *b, int second, uint64_t mask, int byte_offset, uint64_t bm0, uint64_t bm1)
{
    uint64_t pm = second ? b->page_mask2 : b->page_mask;

    if (b->flags & CODEBLOCK_BYTE_MASK) {
        uint32_t phys = second ? b->phys_2 : b->phys;
        int      off  = (phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;

        if (second && (phys == (uint32_t) -1))
            return 1;
        return ((off == byte_offset) && (pm & bm0)) || (bm1 && (off == (byte_offset + 1)) && (pm & bm1));
    }
    return !!(pm & mask);
}

/* A write with these masks (mem.c) to compiled code: if it reaches a
   linked block the page is noted, and the links into its written blocks go
   at the loop's next look; a running chain leaves at its next link. Only a
   link skips the dirty test a dispatch makes, so a page with no linked
   block needs nothing. */
void
codegen_page_code_hit(const struct page_t *p, uint64_t mask, int byte_offset, uint64_t bm0, uint64_t bm1)
{
    const uint32_t pg = (uint32_t) (p - pages) & (CODEGEN_LINK_PHYS_PAGES - 1);

    if (!codegen_link_phys_count[pg])
        return;
    for (int32_t node = codegen_lp_head[pg]; node >= 0;) {
        const codeblock_t *b = &codeblock[node >> 1];

        if (codegen_page_block_hits(b, node & 1, mask, byte_offset, bm0, bm1)) {
            codegen_written_note(p);
            mem_chain_code(MEM_CHAIN_CODE_WRITTEN);
            return;
        }
        node = b->lp_next[node & 1];
    }
}

/* The whole page was written behind the processor's back (mem.c
   mem_invalidate_range, which marked it all dirty first): noted, as any
   write to code is. */
void
codegen_page_code_drop(const struct page_t *p)
{
    codegen_written_note(p);
    mem_chain_code(MEM_CHAIN_CODE_WRITTEN);
}
#endif
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

    codegen_backend_init();
#ifdef CODEGEN_CHAIN
    codegen_ind_reset();
    codegen_lg_init();
#endif
    block_free_list = 0;
    for (uint32_t c = 0; c < BLOCK_SIZE; c++)
        block_free_list_add(&codeblock[c]);
    block_dirty_list_head = block_dirty_list_tail = 0;
    dirty_list_size                               = 0;
#ifdef DEBUG_EXTRA
    memset(instr_counts, 0, sizeof(instr_counts));
#endif
}

void
codegen_reset(void)
{
    int c;

#ifdef CODEGEN_CHAIN
    codegen_links_drop_all(1);
#endif
    for (c = 1; c < BLOCK_SIZE; c++) {
        codeblock_t *block = &codeblock[c];

        if (block->valid) {
            block->phys   = 0;
            block->phys_2 = 0;
            delete_block(block);
        }
    }

#ifdef CODEGEN_CHAIN
    {
        /* The link generations outlive the blocks (codegen.h): a stub a
           block still running from before the reset jumps through must not
           match its target's number again. drop_all has moved every one. */
        uint64_t *gens = malloc(BLOCK_SIZE * sizeof(uint64_t));

        if (gens == NULL)
            fatal("codegen_reset: out of memory\n");
        for (c = 0; c < BLOCK_SIZE; c++)
            gens[c] = codeblock[c].link_gen;
        memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
        for (c = 0; c < BLOCK_SIZE; c++)
            codeblock[c].link_gen = gens[c];
        free(gens);
    }
#else
    memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
#endif
    memset(codeblock_hash, 0, HASH_SIZE * sizeof(uint32_t));
    mem_reset_page_blocks();
#ifdef CODEGEN_CHAIN
    /* Every link went with its block's code. */
    codegen_ind_reset();
    memset(codegen_link_targets, 0, sizeof(codegen_link_targets));
    memset(codegen_link_phys_count, 0, sizeof(codegen_link_phys_count));
    codegen_lg_init();
    mem_link_large_forget();
#endif

    block_free_list = 0;
    for (c = 0; c < BLOCK_SIZE; c++) {
        codeblock[c].valid = 0;
        block_free_list_add(&codeblock[c]);
    }
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
    if (block->head_mem_block)
        codegen_allocator_free(block->head_mem_block);
    block->head_mem_block = NULL;
#ifdef CODEGEN_CHAIN
    codegen_links_drop_owned(block);
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
#ifdef CODEGEN_CHAIN
    codegen_links_drop_owned(block);
#endif
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
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__loongarch_lp64)
    /* ARM64/LoongArch64 state initialization for delayed NO_IMMEDIATES policy. */
    block->dirty_list_recompile_hits = 0;
    /* Zero epoch so the first dirty-list hit starts a new burst. */
    block->dirty_list_last_epoch     = 0;
#endif
    block->dirty_mask  = &page->dirty_mask;
    block->dirty_mask2 = NULL;
    block->next = block->prev = BLOCK_INVALID;
    block->next_2 = block->prev_2 = BLOCK_INVALID;
    block->page_mask = block->page_mask2 = 0;
    block->flags                         = CODEBLOCK_STATIC_TOP;
    block->status                        = cpu_cur_status;

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

    if (block->head_mem_block) {
#ifdef CODEGEN_CHAIN
        /* Its old code goes: no link may reach it again. */
        codegen_links_drop_owned(block);
#endif
        codegen_allocator_free(block->head_mem_block);
        block->head_mem_block = NULL;
    }
    block->head_mem_block = codegen_allocator_allocate(NULL, block_current);
    block->data           = codeblock_allocator_get_ptr(block->head_mem_block);

    block->status = cpu_cur_status;
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
    codegen_fpu_entered   = 0;
    codegen_mmx_entered   = 0;
    codegen_fpu_class_dirty = 0;

    codegen_fpu_loaded_iq[0] = codegen_fpu_loaded_iq[1] = codegen_fpu_loaded_iq[2] = codegen_fpu_loaded_iq[3] = codegen_fpu_loaded_iq[4] = codegen_fpu_loaded_iq[5] = codegen_fpu_loaded_iq[6] = codegen_fpu_loaded_iq[7] = 0;

    cpu_state.seg_ds.checked = cpu_state.seg_es.checked = cpu_state.seg_fs.checked = cpu_state.seg_gs.checked = (cr0 & 1) ? 0 : 1;

    block->TOP = cpu_state.TOP & 7;
    block->flags |= CODEBLOCK_WAS_RECOMPILED;

    codegen_flat_ds = !(cpu_cur_status & CPU_STATUS_NOTFLATDS);
    codegen_flat_ss = !(cpu_cur_status & CPU_STATUS_NOTFLATSS);
    codegen_flat_es = !(cpu_cur_status & CPU_STATUS_NOTFLATES);

    if (block->flags & CODEBLOCK_BYTE_MASK) {
        block->dirty_mask  = &page->byte_dirty_mask[(block->phys >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK];
        block->dirty_mask2 = NULL;
    }

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
#ifdef CODEGEN_CHAIN
    /* What the recording loop found the block's end to be (386_dynarec.c):
       a link at its end, appended after loop unrolling (codegen_ir.c). */
    codegen_end_link_pending = codegen_end_link_ok;
    codegen_end_link_ok      = 0;
#endif
    codegen_ir_compile(ir_data, block);
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
