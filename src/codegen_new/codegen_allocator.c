#if defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
#    include <sys/mman.h>
#    include <unistd.h>
#    include <stdlib.h>
#endif
#if defined WIN32 || defined _WIN32 || defined _WIN32
#    include <windows.h>
#endif

#include <stdbool.h>
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
#include "codegen_backend.h"

struct mem_code_block_t;

typedef struct mem_code_block_t
{
    struct mem_code_block_t* prev;
    struct mem_code_block_t* next;

    int number;
} mem_code_block_t;

static bool valid_code_blocks[BLOCK_SIZE];
static mem_code_block_t mem_code_blocks[BLOCK_SIZE];
static mem_code_block_t* mem_code_block_head = NULL;
static mem_code_block_t* mem_code_block_tail = NULL;

static void
remove_from_block_list(mem_code_block_t* block)
{
    valid_code_blocks[block->number] = 0;
    if (block->prev) {
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        } else {
            mem_code_block_tail = block->prev;
        }
    } else if (block->next) {
        mem_code_block_head = block->next;
        if (mem_code_block_head && mem_code_block_head->next) {
            mem_code_block_head->next->prev = mem_code_block_head;
        }
    } else if (block == mem_code_block_head) {
        mem_code_block_head = mem_code_block_tail = NULL;
    }
    block->next = block->prev = NULL;
}

static void
add_to_block_list(int code_block)
{
    if (!mem_code_block_head) {
        mem_code_block_head = &mem_code_blocks[code_block];
        mem_code_block_head->number = code_block;
        mem_code_block_tail = mem_code_block_head;
    } else {
        mem_code_block_tail->next = &mem_code_blocks[code_block];
        mem_code_blocks[code_block].prev = mem_code_block_tail;
        mem_code_block_tail = &mem_code_blocks[code_block];
        mem_code_blocks[code_block].number = code_block;
    }
}

typedef struct mem_block_t {
    uint32_t offset; /*Offset into mem_block_alloc*/
    uint32_t next;
    uint32_t tail;
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
        mem_blocks[c].tail       = 0;
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

    if (!mem_block_free_list) {
        if (mem_code_block_head == mem_code_block_tail) {
            fatal("Out of memory blocks!\n");
        } else {
            mem_code_block_t* mem_code_block = mem_code_block_head;
            while (mem_code_block) {
                if (code_block != mem_code_block->number) {
                    codegen_delete_block(&codeblock[mem_code_block->number]);
                }
                mem_code_block = mem_code_block->next;
            }

            if (mem_block_free_list)
                goto block_allocate;

            fatal("Out of memory blocks!\n");
        }
    }

block_allocate:
    /*Remove from free list*/
    block_nr            = mem_block_free_list;
    block               = &mem_blocks[block_nr - 1];
    mem_block_free_list = block->next;

    block->code_block = code_block;
    if (parent) {
        /*Add to parent list*/
        if (parent->tail) {
            mem_blocks[parent->tail - 1].next = block_nr;
            parent->tail = block_nr;
        }
        else
            parent->next = parent->tail = block_nr;
        block->next = block->tail = 0;
    } else {
        block->next = block->tail = 0;

        if (!valid_code_blocks[code_block]) {
            valid_code_blocks[code_block] = 1;
            add_to_block_list(code_block);
        }
    }

    codegen_allocator_usage++;
    return block;
}
void
codegen_allocator_free(mem_block_t *block)
{
    int block_nr = (((uintptr_t) block - (uintptr_t) mem_blocks) / sizeof(mem_block_t)) + 1;

    block->tail = 0;
    if (valid_code_blocks[block->code_block])
        remove_from_block_list(&mem_code_blocks[block->code_block]);

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
#if defined __ARM_EABI__ || defined __aarch64__ || defined _M_ARM64
    while (1) {
        __clear_cache(&mem_block_alloc[block->offset], &mem_block_alloc[block->offset + MEM_BLOCK_SIZE]);
        if (block->next)
            block = &mem_blocks[block->next - 1];
        else
            break;
    }
#endif
}
