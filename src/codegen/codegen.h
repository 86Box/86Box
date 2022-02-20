/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the code generator.
 *
 *
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#ifndef _CODEGEN_H_
#define _CODEGEN_H_

#include <86box/mem.h>
#include "x86_ops.h"

#ifdef __amd64__
#include "codegen_x86-64.h"
#elif defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86 || defined _M_X64
#include "codegen_x86.h"
#else
#error Dynamic recompiler not implemented on your platform
#endif

/*Handling self-modifying code (of which there is a lot on x86) :

  PCem tracks a 'dirty mask' for each physical page, in which each bit
  represents 64 bytes. This is only tracked for pages that have code in - when a
  page first has a codeblock generated, it is evicted from the writelookup and
  added to the page_lookup for this purpose. When in the page_lookup, each write
  will go through the mem_write_ram*_page() functions and set the dirty mask
  appropriately.

  Each codeblock also contains a code mask (actually two masks, one for each
  page the block is/may be in), again with each bit representing 64 bytes.

  Each page has a list of codeblocks present in it. As each codeblock can span
  up to two pages, two lists are present.

  When a codeblock is about to be executed, the code masks are compared with the
  dirty masks for the relevant pages. If either intersect, then
  codegen_check_flush() is called on the affected page(s), and all affected
  blocks are evicted.

  The 64 byte granularity appears to work reasonably well for most cases,
  avoiding most unnecessary evictions (eg when code & data are stored in the
  same page).
*/

typedef struct codeblock_t
{
        uint64_t page_mask, page_mask2;
        uint64_t *dirty_mask, *dirty_mask2;
        uint64_t cmp;

        /*Previous and next pointers, for the codeblock list associated with
          each physical page. Two sets of pointers, as a codeblock can be
          present in two pages.*/
        struct codeblock_t *prev, *next;
        struct codeblock_t *prev_2, *next_2;

        /*Pointers for codeblock tree, used to search for blocks when hash lookup
          fails.*/
        struct codeblock_t *parent, *left, *right;

        int pnt;
        int ins;

	int valid;

        int was_recompiled;
        int TOP;

        uint32_t pc;
        uint32_t _cs;
        uint32_t endpc;
        uint32_t phys, phys_2;
        uint32_t status;
        uint32_t flags;

        uint8_t data[2048];
} codeblock_t;

/*Code block uses FPU*/
#define CODEBLOCK_HAS_FPU 1
/*Code block is always entered with the same FPU top-of-stack*/
#define CODEBLOCK_STATIC_TOP 2

static inline codeblock_t *codeblock_tree_find(uint32_t phys, uint32_t _cs)
{
        codeblock_t *block = pages[phys >> 12].head;
        uint64_t a = _cs | ((uint64_t)phys << 32);

        while (block)
        {
                if (a == block->cmp)
                {
                        if (!((block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) &&
                             ((block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK)))
                                break;
                }
                if (a < block->cmp)
                        block = block->left;
                else
                        block = block->right;
        }

        return block;
}

static inline void codeblock_tree_add(codeblock_t *new_block)
{
        codeblock_t *block = pages[new_block->phys >> 12].head;
        uint64_t a = new_block->_cs | ((uint64_t)new_block->phys << 32);
        new_block->cmp = a;

        if (!block)
        {
                pages[new_block->phys >> 12].head = new_block;
                new_block->parent = new_block->left = new_block->right = NULL;
        }
        else
        {
                codeblock_t *old_block = NULL;

                while (block)
                {
                        old_block = block;
                        if (a < old_block->cmp)
                                block = block->left;
                        else
                                block = block->right;
                }

                if (a < old_block->cmp)
                        old_block->left = new_block;
                else
                        old_block->right = new_block;

                new_block->parent = old_block;
                new_block->left = new_block->right = NULL;
        }
}

static inline void codeblock_tree_delete(codeblock_t *block)
{
        codeblock_t *parent = block->parent;

        if (!block->left && !block->right)
        {
                /*Easy case - remove from parent*/
                if (!parent)
                        pages[block->phys >> 12].head = NULL;
                else
                {
                        if (parent->left == block)
                                parent->left = NULL;
                        if (parent->right == block)
                                parent->right = NULL;
                }
                return;
        }
        else if (!block->left)
        {
                /*Only right node*/
                if (!parent)
                {
                        pages[block->phys >> 12].head = block->right;
                        pages[block->phys >> 12].head->parent = NULL;
                }
                else
                {
                        if (parent->left == block)
                        {
                                parent->left = block->right;
                                parent->left->parent = parent;
                        }
                        if (parent->right == block)
                        {
                                parent->right = block->right;
                                parent->right->parent = parent;
                        }
                }
                return;
        }
        else if (!block->right)
        {
                /*Only left node*/
                if (!parent)
                {
                        pages[block->phys >> 12].head = block->left;
                        pages[block->phys >> 12].head->parent = NULL;
                }
                else
                {
                        if (parent->left == block)
                        {
                                parent->left = block->left;
                                parent->left->parent = parent;
                        }
                        if (parent->right == block)
                        {
                                parent->right = block->left;
                                parent->right->parent = parent;
                        }
                }
                return;
        }
        else
        {
                /*Difficult case - node has two children. Walk right child to find lowest node*/
                codeblock_t *lowest = block->right, *highest;
                codeblock_t *old_parent;

                while (lowest->left)
                        lowest = lowest->left;

                old_parent = lowest->parent;

                /*Replace deleted node with lowest node*/
                if (!parent)
                        pages[block->phys >> 12].head = lowest;
                else
                {
                        if (parent->left == block)
                                parent->left = lowest;
                        if (parent->right == block)
                                parent->right = lowest;
                }

                lowest->parent = parent;
                lowest->left = block->left;
                if (lowest->left)
                        lowest->left->parent = lowest;

                old_parent->left = NULL;

                highest = lowest->right;
                if (!highest)
                {
                        if (lowest != block->right)
                        {
                                lowest->right = block->right;
                                block->right->parent = lowest;
                        }
                        return;
                }

                while (highest->right)
                        highest = highest->right;

                if (block->right && block->right != lowest)
                {
                        highest->right = block->right;
                        block->right->parent = highest;
                }
        }
}

#define PAGE_MASK_INDEX_MASK 3
#define PAGE_MASK_INDEX_SHIFT 10
#define PAGE_MASK_MASK 63
#define PAGE_MASK_SHIFT 4

extern codeblock_t *codeblock;

extern codeblock_t **codeblock_hash;

void codegen_init();
void codegen_reset();
void codegen_block_init(uint32_t phys_addr);
void codegen_block_remove();
void codegen_block_start_recompile(codeblock_t *block);
void codegen_block_end_recompile(codeblock_t *block);
void codegen_block_end();
void codegen_generate_call(uint8_t opcode, OpFn op, uint32_t fetchdat, uint32_t new_pc, uint32_t old_pc);
void codegen_generate_seg_restore();
void codegen_set_op32();
void codegen_flush();
void codegen_check_flush(page_t *page, uint64_t mask, uint32_t phys_addr);

extern int cpu_block_end;
extern uint32_t codegen_endpc;

extern int codegen_block_cycles;

extern void (*codegen_timing_start)();
extern void (*codegen_timing_prefix)(uint8_t prefix, uint32_t fetchdat);
extern void (*codegen_timing_opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
extern void (*codegen_timing_block_start)();
extern void (*codegen_timing_block_end)();
extern int (*codegen_timing_jump_cycles)();

typedef struct codegen_timing_t
{
        void (*start)();
        void (*prefix)(uint8_t prefix, uint32_t fetchdat);
        void (*opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
        void (*block_start)();
        void (*block_end)();
        int (*jump_cycles)();
} codegen_timing_t;

extern codegen_timing_t codegen_timing_pentium;
extern codegen_timing_t codegen_timing_686;
extern codegen_timing_t codegen_timing_486;
extern codegen_timing_t codegen_timing_winchip;
extern codegen_timing_t codegen_timing_winchip2;
extern codegen_timing_t codegen_timing_k6;
extern codegen_timing_t codegen_timing_p6;

void codegen_timing_set(codegen_timing_t *timing);

extern int block_current;
extern int block_pos;

#define CPU_BLOCK_END() cpu_block_end = 1

static inline void addbyte(uint8_t val)
{
        codeblock[block_current].data[block_pos++] = val;
        if (block_pos >= BLOCK_MAX)
        {
                CPU_BLOCK_END();
        }
}

static inline void addword(uint16_t val)
{
	uint16_t *p = (uint16_t *) &codeblock[block_current].data[block_pos];
        *p = val;
        block_pos += 2;
        if (block_pos >= BLOCK_MAX)
        {
                CPU_BLOCK_END();
        }
}

static inline void addlong(uint32_t val)
{
	uint32_t *p = (uint32_t *) &codeblock[block_current].data[block_pos];
        *p = val;
        block_pos += 4;
        if (block_pos >= BLOCK_MAX)
        {
                CPU_BLOCK_END();
        }
}

static inline void addquad(uint64_t val)
{
	uint64_t *p = (uint64_t *) &codeblock[block_current].data[block_pos];
        *p = val;
        block_pos += 8;
        if (block_pos >= BLOCK_MAX)
        {
                CPU_BLOCK_END();
        }
}

/*Current physical page of block being recompiled. -1 if no recompilation taking place */
extern uint32_t recomp_page;

extern x86seg *op_ea_seg;
extern int op_ssegs;
extern uint32_t op_old_pc;

/*Set to 1 if flags have been changed in the block being recompiled, and hence
  flags_op is known and can be relied on */
extern int codegen_flags_changed;

extern int codegen_fpu_entered;
extern int codegen_mmx_entered;

extern int codegen_fpu_loaded_iq[8];
extern int codegen_reg_loaded[8];

extern int codegen_in_recompile;

#endif
