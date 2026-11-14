#ifndef _CODEGEN_LINK_H_
#define _CODEGEN_LINK_H_

#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK

/*Block linking. A block that ends normally branches to a shared routine
  with the exit's own slot in the return register (x0 on arm64, RAX on
  x86-64). If cpu_state.pc and the CS base match the slot, the routine repeats, inline,
  every check exec386_dynarec() and exec386_dynarec_dyn() would make between
  this block and the next one, and if they all pass it does their stores and
  jumps to the next block's link entry, skipping its register saves.
  Anything else returns to C as before, still returning the slots
  (codegen_exit_rout returns NULL).

  A slot never needs cutting to stay correct: it is checked on every use,
  and link_gen changes whenever the target block is freed, recompiled or has
  any field below changed.*/

/*Three per block, one each for its end and two mid-block exits; a fourth
  exit shares the first.*/
#define CODEGEN_LINK_SLOTS 3

#if defined(__aarch64__) || defined(_M_ARM64)
/*The instructions a target needs for the arm64 link routine to prefetch
  the code of the block after it while it runs*/
#    define CODEGEN_LINK_NEXT_MIN_INS 24

/*arm64: one cache line a record. What the routine can work out is not kept:
  the block from block_nr, the translation it checks from readlookup2[] or
  rammask and the exit's address, mem_exec from phys_page, the end's slot
  from block_nr and end_slot*/
typedef struct codegen_link_slot_t {
    uint32_t  exit_pc;   /*cpu_state.pc at the exit*/
    uint32_t  exit_cs;   /*CS base at the exit*/
    uint8_t  *entry;     /*Link entry of the target; NULL = empty slot*/
    uint32_t  gen;       /*block->link_gen when the slot was filled*/
    uint16_t  status;    /*cpu_cur_status after the FPU refresh*/
    uint16_t  npxc;      /*cpu_state.npxc & 0x0f00*/
    uint32_t  cr0_bits;  /*cr0 & 0xc0000000 (CD can only be clear here)*/
    uint32_t  phys_page; /*get_phys(exit_cs + exit_pc) & ~0xfff*/
    uint64_t  page_mask; /*block->page_mask*/
    uint64_t *dirty_mask; /*block->dirty_mask*/
    /*The routine loads these as one word and tests its bits below 33*/
    uint16_t  block_nr;  /*get_block_nr(block)*/
    uint8_t   top_mask;  /*7 if the block has a static TOP, otherwise 0*/
    uint8_t   top;       /*block->TOP if top_mask is 7, otherwise 0*/
    uint8_t   two_page;  /*bit 0: block->page_mask2 is set. Bit 1: the exit's
                           block calls no C, so CS and the CPL are as at the
                           loop top and the routine leaves oldcs and oldcpl.
                           Bit 2: nor does it change IF or TF
                           (CODEBLOCK_LINK_STATE), so they and the pending
                           events only changed if cpu_state.link_c_ran is set.
                           Bit 3 (arm64): the block has at least
                           CODEGEN_LINK_NEXT_MIN_INS instructions, and the
                           routine prefetches the next block's code*/
    uint8_t   end_slot;  /*block->end_slot, the slot the routine prefetches*/
    /*The exit's set in codegen_link_sets, 0 = none. Every slot record of a
      set holds the set's number, so a record keeps it right wherever it is
      copied*/
    uint16_t  set;
    /*codegen_link_epoch when the record last passed every check, 0 = never
      (codegen_link_epoch)*/
    uint32_t  epoch;
    /*In the table: the index of the record the routine found there after
      this one last time, which it prefetches as it goes to this one's block.
      Nothing else reads it; anywhere else it means nothing*/
    uint32_t  hint;
} codegen_link_slot_t;
#else
typedef struct codegen_link_slot_t {
    uint32_t     exit_pc;     /*cpu_state.pc at the exit*/
    uint32_t     exit_cs;     /*CS base at the exit*/
    uint8_t     *entry;       /*Link entry of the target; NULL = empty slot*/
    codeblock_t *block;       /*Target block*/
    uint32_t     gen;         /*block->link_gen when the slot was filled*/
    uint16_t     status;      /*cpu_cur_status after the FPU refresh*/
    uint16_t     npxc;        /*cpu_state.npxc & 0x0f00*/
    uint32_t     cr0_bits;    /*cr0 & 0xc0000000 (CD can only be clear here)*/
    uint32_t     virt;        /*exit_cs + exit_pc*/
    uint32_t     phys_page;   /*get_phys(virt) & ~0xfff*/
    uint32_t     rammask;     /*rammask, compared when paging is off*/
    uintptr_t   *xlat;        /*&readlookup2[virt >> 12], compared when paging is on*/
    uintptr_t    xlat_val;    /*readlookup2[virt >> 12], never (int)-1*/
    uint8_t    **mem_exec;    /*&_mem_exec[phys >> MEM_GRANULARITY_BITS]*/
    uint32_t    *hash;        /*&codeblock_hash[HASH(phys)]*/
#if defined(__aarch64__) || defined(_M_ARM64)
    /*Fits 16 bits there (BLOCK_SIZE 0x10000); the arm64 routine loads it with
      the three fields after it as one word*/
    uint16_t     block_nr;    /*get_block_nr(block)*/
#else
    uint32_t     block_nr;    /*get_block_nr(block)*/
#endif
    uint8_t      top_mask;    /*7 if the block has a static TOP, otherwise 0*/
    uint8_t      top;         /*block->TOP if top_mask is 7, otherwise 0*/
    uint8_t      two_page;    /*1 if block->page_mask2 is set*/
#if defined(CODEGEN_BACKEND_HAS_LINK_SETS) && (defined(__aarch64__) || defined(_M_ARM64))
    /*In the padding of the word block_nr starts, which the routine's hit
      path tests only below bit 33. Every slot record of a set holds the
      set's number, so a record keeps it right wherever it is copied. There
      is no room for it on x86-64, whose routine reads
      codegen_link_site_set.*/
    uint16_t     set;         /*the exit's set in codegen_link_sets, 0 = none*/
#endif
    uint64_t     page_mask;   /*block->page_mask*/
    uint64_t    *dirty_mask;  /*block->dirty_mask*/
} codegen_link_slot_t;
#endif

/*codegen_public.h*/
extern uint32_t codegen_link_epoch;
#    if defined(__aarch64__) || defined(_M_ARM64)
/*Side by side, as a record's status | npxc << 16 and cr0_bits are, so the
  short way compares both in one: [0] the status (without the PC24 and RC_NZ
  bits, as records hold it), [1] the CR0 bits and the CPL*/
extern uint32_t codegen_link_state[2];
#define codegen_link_state_st (codegen_link_state[0])
#define codegen_link_state_cr (codegen_link_state[1])
#    endif

/*Loop-top state of exec386_dynarec(), stored by the link routine.*/
extern int32_t  cycles_old;
extern uint64_t tsc_old;
#if defined(__aarch64__) || defined(_M_ARM64)
/*cycles the link routine's short way needs to stay above, INT32_MAX until
  its full way has checked the slice and the timer*/
extern int32_t codegen_link_cycles_min;
#endif

/*Indexed by the number of the block whose exit uses them.*/
extern codegen_link_slot_t codegen_link_slots[BLOCK_SIZE][CODEGEN_LINK_SLOTS];

#ifdef CODEGEN_BACKEND_HAS_LINK_SETS
/*A slot whose target changed more than once gets a set holding the three
  records it held most recently before its current one. After a key
  mismatch the routine looks for the target there and, if it is found,
  swaps that record with the slot's and goes on with the same checks; the
  slot always holds the target its exit last went to, so a set changes
  nothing for an exit that matches the slot. Records are only ever made by
  codegen_link_patch() and copied whole, and none of their fields depends on
  where they sit.*/
#    define CODEGEN_LINK_SETS 16384

typedef struct codegen_link_set_t {
    uint8_t             order;   /*codegen_link_set_mtf state*/
#    if defined(__aarch64__) || defined(_M_ARM64)
    /*The records on line boundaries, and 256 bytes a set*/
    uint8_t             pad[63];
#    else
    uint8_t             pad[7];
#    endif
    codegen_link_slot_t e[3];
} codegen_link_set_t;

/*Number 0 is never used*/
extern codegen_link_set_t codegen_link_sets[CODEGEN_LINK_SETS];
/*[order][n]: order after e[n] was used*/
extern const uint8_t codegen_link_set_mtf[6][4];

/*Per slot, indexed as codegen_link_slots[0][]: 0 = its target has not
  changed since it was emptied, CODEGEN_LINK_SET_ONE = once, otherwise the
  number of its set. A target that changes once, and then stays, gets no
  set.*/
#    define CODEGEN_LINK_SET_ONE 0xffff
extern uint16_t codegen_link_site_set[BLOCK_SIZE * CODEGEN_LINK_SLOTS];
#endif

#ifdef CODEGEN_BACKEND_HAS_LINK_TABLE
/*Every record codegen_link_patch() makes is also copied here, at the hash
  of its key, the last one there winning. An exit whose slot and set both
  miss looks for its target here before it goes back to the C loop: one
  with more targets than they hold (a jump table, a RET from a function
  called from many places) found it there rather than in the C loop. A
  record is checked here as in a slot and is never copied out, so the set
  number in it means nothing.*/
#    define CODEGEN_LINK_TABLE_SIZE 16384
#    define CODEGEN_LINK_TABLE_HASH(pc, cs_base) \
        ((((pc) ^ (cs_base)) ^ (((pc) ^ (cs_base)) >> 11)) & (CODEGEN_LINK_TABLE_SIZE - 1))

extern codegen_link_slot_t codegen_link_table[CODEGEN_LINK_TABLE_SIZE];
#endif

/*Byte offset of the link entry from &block->data[BLOCK_START]: the first
  instruction after the prologue's register saves.*/
#if defined(__aarch64__) || defined(_M_ARM64)
/*None: arm64 blocks have no prologue, codegen_enter_block() saves them*/
#    define CODEGEN_LINK_ENTRY_OFFSET 0
#elif defined(_WIN64)
/*Eight pushes, the frame and the XMM6/XMM7 saves*/
#    define CODEGEN_LINK_ENTRY_OFFSET 28
#else
/*Six pushes and the frame*/
#    define CODEGEN_LINK_ENTRY_OFFSET 14
#endif

/*Fill a slot of the pair the previous block returned, for the block about
  to run at cs:pc from physical address phys. Does nothing if the block or
  the state does not qualify.*/
extern void codegen_link_patch(codegen_link_slot_t *from, codeblock_t *block, uint32_t phys);
/*Give a block a new link_gen, so no slot links to it any more.*/
extern void codegen_link_block_changed(codeblock_t *block);
/*Empty every slot.*/
extern void codegen_link_reset(void);

#endif

#endif
