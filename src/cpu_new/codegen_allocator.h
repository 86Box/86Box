#ifndef _CODEGEN_ALLOCATOR_H_
#define _CODEGEN_ALLOCATOR_H_

/*The allocator handles all allocation of executable memory. Since the two-pass
  recompiler design makes applying hard limits to codeblock size difficult, the
  allocator allows memory to be provided as and when required.
  
  The allocator provides a block size of a little under 1 kB (slightly lower to
  limit cache aliasing). Each generated codeblock is allocated one block by default,
  and will allocate additional block(s) once the existing memory is sorted. Blocks
  are chained together by jump instructions.
  
  Due to the chaining, the total memory size is limited by the range of a jump
  instruction. ARMv7 is restricted to +/- 32 MB, ARMv8 to +/- 128 MB, x86 to
  +/- 2GB. As a result, total memory size is limited to 32 MB on ARMv7*/
#ifdef __ARM_EABI__
#define MEM_BLOCK_NR 32768
#else
#define MEM_BLOCK_NR 131072
#endif

#define MEM_BLOCK_MASK (MEM_BLOCK_NR-1)
#define MEM_BLOCK_SIZE 0x3c0

void codegen_allocator_init();
/*Allocate a mem_block_t, and the associated backing memory.
  If parent is non-NULL, then the new block will be added to the list in
  parent->next*/
struct mem_block_t *codegen_allocator_allocate(struct mem_block_t *parent, int code_block);
/*Free a mem_block_t, and any subsequent blocks in the list at block->next*/
void codegen_allocator_free(struct mem_block_t *block);
/*Get a pointer to the backing memory associated with block*/
uint8_t *codeblock_allocator_get_ptr(struct mem_block_t *block);
/*Cache clean memory block list*/
void codegen_allocator_clean_blocks(struct mem_block_t *block);

extern int codegen_allocator_usage;

#endif
