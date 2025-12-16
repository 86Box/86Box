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

static uint8_t* codeblock_memory_blocks[0x4000];
static mem_block_t codeblock_memblock_struct[0x4000];

void codegen_allocator_init(void)
{
    for (int i = 0; i < 0x4000; i++) {
        codeblock_memory_blocks[i] = plat_mmap_replaceable(65536, &codeblock_memblock_struct[i].handle);
        codeblock_memblock_struct[i].code_block = codeblock_memory_blocks[i];
        codeblock_memblock_struct[i].usable_size = 0;
        plat_mmap_slice(codeblock_memory_blocks[i], 16384, 4);
        codeblock_memblock_struct[i].code_block = plat_mmap_fixed(16384, 1, codeblock_memblock_struct[i].code_block, 0, &codeblock_memblock_struct[i].handle);
        if (!codeblock_memblock_struct[i].code_block)
            fatal("mmap failed!\n");
        codeblock_memblock_struct[i].usable_size = 16384;
    }
}

struct mem_block_t *codegen_allocator_allocate(struct mem_block_t *parent, int code_block)
{
    if (parent) {
        if (parent->usable_size >= 65536) {
            fatal("Out of memory for a single block\n");
        }
        parent->code_block = plat_mmap_fixed(16384, 1, parent->code_block, parent->usable_size, &parent->handle);
        if (!parent->code_block)
            fatal("mmap failed!\n");
        parent->usable_size += 16384;
        return parent;
    } else {
        parent = &codeblock_memblock_struct[code_block];
        if (parent->usable_size)
            return parent;
        parent->code_block = plat_mmap_fixed(16384, 1, parent->code_block, 0, &parent->handle);
        parent->usable_size = 16384;
        if (!parent->code_block)
            fatal("mmap failed!\n");
    }
    return parent;
}

uint8_t *codeblock_allocator_get_ptr(struct mem_block_t *block)
{
    return block->code_block;
}

void codegen_allocator_free(struct mem_block_t *block)
{
    // We can't free those, yet. I don't think macOS allows replacing pages either.
}

void codegen_allocator_clean_blocks(struct mem_block_t *block)
{
#if defined __ARM_EABI__ || defined __aarch64__ || defined _M_ARM64
#    ifndef _MSC_VER
    __clear_cache(block->code_block, block->code_block + block->usable_size);
#    else
    FlushInstructionCache(GetCurrentProcess(), block->code_block, block->usable_size);
#    endif
#endif
}

void codegen_allocator_clean_blocks_sized(struct mem_block_t *block, uint64_t size)
{
#if defined __ARM_EABI__ || defined __aarch64__ || defined _M_ARM64
#    ifndef _MSC_VER
    __clear_cache(block->code_block, block->code_block + size);
#    else
    FlushInstructionCache(GetCurrentProcess(), block->code_block, size);
#    endif
#endif
}
