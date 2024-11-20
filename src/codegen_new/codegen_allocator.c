#if defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
#    include <sys/mman.h>
#    include <unistd.h>
#    include <stdlib.h>
#endif
#if defined WIN32 || defined _WIN32 || defined _WIN32
#    include <windows.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>

#include "codegen.h"
#include "codegen_allocator.h"

typedef struct mem_block_t {
    uint32_t offset; /*Offset into mem_block_alloc*/
    uint32_t next;
    uint16_t code_block;
} mem_block_t;

static mem_block_t mem_blocks[MEM_BLOCK_NR];
static uint32_t    mem_block_free_list;
static uint8_t    *mem_block_alloc = NULL;

int codegen_allocator_usage = 0;

void
codegen_allocator_init(void)
{
    mem_block_alloc = plat_mmap(MEM_BLOCK_NR * MEM_BLOCK_SIZE, 1);

    for (uint32_t c = 0; c < MEM_BLOCK_NR; c++) {
        mem_blocks[c].offset     = c * MEM_BLOCK_SIZE;
        mem_blocks[c].code_block = BLOCK_INVALID;
        if (c < MEM_BLOCK_NR - 1)
            mem_blocks[c].next = c + 2;
        else
            mem_blocks[c].next = 0;
    }
    mem_block_free_list = 1;
}

mem_block_t *
codegen_allocator_allocate(mem_block_t *parent, int code_block)
{
    mem_block_t *block;
    uint32_t     block_nr;

    while (!mem_block_free_list) {
        /*Pick a random memory block and free the owning code block*/
        block_nr = rand() & MEM_BLOCK_MASK;
        block    = &mem_blocks[block_nr];

        if (block->code_block && block->code_block != code_block)
            codegen_delete_block(&codeblock[block->code_block]);
    }

    /*Remove from free list*/
    block_nr            = mem_block_free_list;
    block               = &mem_blocks[block_nr - 1];
    mem_block_free_list = block->next;

    block->code_block = code_block;
    if (parent) {
        /*Add to parent list*/
        block->next  = parent->next;
        parent->next = block_nr;
    } else
        block->next = 0;

    codegen_allocator_usage++;
    return block;
}
void
codegen_allocator_free(mem_block_t *block)
{
    int block_nr = (((uintptr_t) block - (uintptr_t) mem_blocks) / sizeof(mem_block_t)) + 1;

    while (1) {
        int next_block_nr = block->next;
        codegen_allocator_usage--;

        block->next         = mem_block_free_list;
        block->code_block   = BLOCK_INVALID;
        mem_block_free_list = block_nr;
        block_nr            = next_block_nr;

        if (block_nr)
            block = &mem_blocks[block_nr - 1];
        else
            break;
    }
}

uint8_t *
codeblock_allocator_get_ptr(mem_block_t *block)
{
    return &mem_block_alloc[block->offset];
}

void
codegen_allocator_clean_blocks(UNUSED(struct mem_block_t *block))
{
#if defined __ARM_EABI__ || defined _ARM_ || defined __aarch64__ || defined _M_ARM || defined _M_ARM64
    while (1) {
#    ifndef _MSC_VER
        __clear_cache(&mem_block_alloc[block->offset], &mem_block_alloc[block->offset + MEM_BLOCK_SIZE]);
#    else
        FlushInstructionCache(GetCurrentProcess(), &mem_block_alloc[block->offset], MEM_BLOCK_SIZE);
#    endif
        if (block->next)
            block = &mem_blocks[block->next - 1];
        else
            break;
    }
#endif
}
