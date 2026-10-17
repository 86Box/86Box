/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the code generator.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
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
#ifndef _CODEGEN_PUBLIC_H_
#define _CODEGEN_PUBLIC_H_

#define PAGE_MASK_SHIFT 6
#define PAGE_MASK_MASK 63

#define BLOCK_PC_INVALID 0xffffffff
#define BLOCK_INVALID    0

/*Chains of linked blocks: the recompiler on x86-64 hosts, not with the
  GDB stub. A block ends in a link straight to the next instead of a return
  to the dispatcher, while nothing needs the dispatcher (the signal word,
  mem.h) and the next timer is not due.*/
#if defined(USE_DYNAREC) && !defined(USE_GDBSTUB) && (defined(__x86_64__) || defined(_M_X64))
#    define CODEGEN_CHAIN 1
#endif

#ifdef CODEGEN_CHAIN
struct page_t;
/* A write with these masks (mem.c) reached compiled code on the page: the
   links into the blocks it reached go (codegen_block.c). */
extern void codegen_page_code_hit(const struct page_t *p, uint64_t mask, int byte_offset, uint64_t bm0, uint64_t bm1);
/* The whole page was written behind the processor's back. */
extern void codegen_page_code_drop(const struct page_t *p);
/* Every link goes (`owner`: from the dispatcher's side), or translations
   may have changed (all, or the linear page `lin`). */
extern void codegen_links_drop_all(int owner);
extern void codegen_links_revalidate_request(int all, uint32_t lin);
#endif

extern void codegen_init(void);
extern void codegen_flush(void);

/*Current physical page of block being recompiled. -1 if no recompilation taking place */
extern uint32_t recomp_page;
extern int      codegen_in_recompile;

#endif
