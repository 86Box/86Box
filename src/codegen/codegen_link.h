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
    /*Fits 16 bits there (BLOCK_SIZE 0x8000); the arm64 routine loads it with
      the three fields after it as one word*/
    uint16_t     block_nr;    /*get_block_nr(block)*/
#else
    uint32_t     block_nr;    /*get_block_nr(block)*/
#endif
    uint8_t      top_mask;    /*7 if the block has a static TOP, otherwise 0*/
    uint8_t      top;         /*block->TOP if top_mask is 7, otherwise 0*/
    uint8_t      two_page;    /*1 if block->page_mask2 is set*/
    uint64_t     page_mask;   /*block->page_mask*/
    uint64_t    *dirty_mask;  /*block->dirty_mask*/
} codegen_link_slot_t;

/*Loop-top state of exec386_dynarec(), stored by the link routine.*/
extern int32_t  cycles_old;
extern uint64_t tsc_old;

/*Indexed by the number of the block whose exit uses them.*/
extern codegen_link_slot_t codegen_link_slots[BLOCK_SIZE][CODEGEN_LINK_SLOTS];

/*Byte offset of the link entry from &block->data[BLOCK_START]: the first
  instruction after the prologue's register saves.*/
#if defined(__aarch64__) || defined(_M_ARM64)
#    define CODEGEN_LINK_ENTRY_OFFSET 24
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
