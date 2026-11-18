/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The synthetic RISC ROM and CGL FIFO microcode loader from rust-dos. */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

void
verite_init_rom(verite_core *core)
{
    static const uint32_t stub[] = {
        0x77feffe0, 0x4bfefe0f, 0x00fefef8, 0x7a0000fe, 0x68ffffff, 0
    };
    memset(core->rom, 0xff, sizeof(core->rom));
    for (unsigned i = 0; i < sizeof(stub) / sizeof(stub[0]); i++)
        for (unsigned j = 0; j < 4; j++)
            core->rom[0x10000 + 4 * i + j] = stub[i] >> (24 - 8 * j);
    memcpy(core->rom + ROM_SIZE - 16, "\x68\xff\xff\xff\0\0\0\0", 8);
}

void
verite_loader_reset(verite_core *core)
{
    free(core->loader.words);
    core->loader = (verite_loader) { 0 };
}

void
verite_loader_begin(verite_core *core)
{
    verite_loader_reset(core);
    core->loader.active = true;
}

/* The keyed stream XORs each byte with a repeating 128-byte key. In a
   long section the most common byte at each position is the key byte. */
static void
loader_find_key(verite_loader *loader)
{
    unsigned nonzero = 0;
    for (unsigned i = 0; i < 128; i++) {
        unsigned counts[256] = { 0 };
        for (size_t w = i / 4; w < loader->count; w += 32)
            counts[(uint8_t) (loader->words[w] >> (8 * (i & 3)))]++;
        unsigned best = 0;
        for (unsigned b = 1; b < 256; b++)
            if (counts[b] > counts[best])
                best = b;
        loader->key[i] = best;
        nonzero += best != 0;
    }
    loader->keyed = nonzero >= 96;
}

static void
loader_store_section(verite_core *core, verite_memory mem)
{
    verite_loader *l = &core->loader;
    if (l->count >= 256)
        loader_find_key(l);
    for (size_t i = 0; i < l->count; i++) {
        uint32_t word = l->words[i];
        if (l->keyed) {
            uint32_t key = 0;
            for (unsigned b = 0; b < 4; b++)
                key |= (uint32_t) l->key[(4 * i + b) & 127] << (8 * b);
            word = verite_bswap(word ^ key);
        }
        mem_write32(mem, l->address + 4 * (uint32_t) i, word);
    }
    free(l->words);
    l->words = NULL;
    l->count = l->capacity = 0;
    core->drew             = true;
}

void
verite_loader_pump(verite_core *core, verite_memory mem)
{
    verite_loader *l = &core->loader;
    uint32_t       word;
    while (l->active && verite_pop_input(core, &word)) {
        switch (l->expect) {
            case LOADER_ADDRESS:
                l->address = word;
                l->expect  = LOADER_LENGTH;
                break;
            case LOADER_LENGTH:
                if (!word) {
                    l->expect = LOADER_ADDRESS;
                    if (!l->address) {
                        l->active    = false;
                        core->pc     = l->start;
                        core->npc    = risc_next(l->start);
                        core->halted = false;
                    }
                    break;
                }
                l->left = word;
                if (!l->has_start) {
                    l->start     = l->address;
                    l->has_start = true;
                }
                l->count  = 0;
                l->expect = LOADER_WORDS;
                break;
            case LOADER_WORDS:
                if (l->count == l->capacity) {
                    const size_t cap   = l->capacity ? l->capacity * 2 : 64;
                    uint32_t    *words = cap > l->capacity && cap <= SIZE_MAX / sizeof(*words)
                        ? realloc(l->words, cap * sizeof(*words))
                        : NULL;
                    if (!words) {
                        verite_loader_reset(core);
                        core->halted = true;
                        return;
                    }
                    l->words    = words;
                    l->capacity = cap;
                }
                l->words[l->count++] = word;
                if (!--l->left) {
                    l->expect = LOADER_ADDRESS;
                    loader_store_section(core, mem);
                }
                break;
        }
    }
}
