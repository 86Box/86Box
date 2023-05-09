#if defined __amd64__ || defined _M_X64

#    include <stdarg.h>
#    include <stdio.h>
#    include <string.h>
#    include <stdint.h>
#    include <stdlib.h>
#    define HAVE_STDARG_H
#    include <86box/86box.h>
#    include "cpu.h"
#    include "x86.h"
#    include "x86_flags.h"
#    include "x86_ops.h"
#    include "x87.h"
#    include <86box/mem.h>

#    include "386_common.h"

#    include "codegen.h"
#    include "codegen_accumulate.h"
#    include "codegen_ops.h"
#    include "codegen_ops_x86-64.h"

#    if defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
#        include <sys/mman.h>
#        include <unistd.h>
#    endif
#    if _WIN64
#        include <windows.h>
#    endif

int      codegen_flat_ds, codegen_flat_ss;
int      codegen_flags_changed = 0;
int      codegen_fpu_entered   = 0;
int      codegen_fpu_loaded_iq[8];
int      codegen_reg_loaded[8];
x86seg  *op_ea_seg;
int      op_ssegs;
uint32_t op_old_pc;

uint32_t recomp_page = -1;

int           host_reg_mapping[NR_HOST_REGS];
int           host_reg_xmm_mapping[NR_HOST_XMM_REGS];
codeblock_t  *codeblock;
codeblock_t **codeblock_hash;
int           codegen_mmx_entered = 0;

int        block_current = 0;
static int block_num;
int        block_pos;

uint32_t codegen_endpc;

int        codegen_block_cycles;
static int codegen_block_ins;
static int codegen_block_full_ins;

static uint32_t last_op32;
static x86seg  *last_ea_seg;
static int      last_ssegs;

void
codegen_init(void)
{
    int c;

#    if _WIN64
    codeblock = VirtualAlloc(NULL, BLOCK_SIZE * sizeof(codeblock_t), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#    elif defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
    codeblock = mmap(NULL, BLOCK_SIZE * sizeof(codeblock_t), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
#    else
    codeblock = malloc(BLOCK_SIZE * sizeof(codeblock_t));
#    endif
    codeblock_hash = malloc(HASH_SIZE * sizeof(codeblock_t *));

    memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
    memset(codeblock_hash, 0, HASH_SIZE * sizeof(codeblock_t *));

    for (c = 0; c < BLOCK_SIZE; c++)
        codeblock[c].valid = 0;
}

void
codegen_reset(void)
{
    int c;

    memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
    memset(codeblock_hash, 0, HASH_SIZE * sizeof(codeblock_t *));
    mem_reset_page_blocks();

    for (c = 0; c < BLOCK_SIZE; c++)
        codeblock[c].valid = 0;
}

void
dump_block(void)
{
}

static void
add_to_block_list(codeblock_t *block)
{
    codeblock_t *block_prev = pages[block->phys >> 12].block[(block->phys >> 10) & 3];

    if (!block->page_mask)
        fatal("add_to_block_list - mask = 0\n");

    if (block_prev) {
        block->next                                             = block_prev;
        block_prev->prev                                        = block;
        pages[block->phys >> 12].block[(block->phys >> 10) & 3] = block;
    } else {
        block->next                                             = NULL;
        pages[block->phys >> 12].block[(block->phys >> 10) & 3] = block;
    }

    if (block->next) {
        if (block->next->valid == 0)
            fatal("block->next->valid=0 %p %p %x %x\n", (void *) block->next, (void *) codeblock, block_current, block_pos);
    }

    if (block->page_mask2) {
        block_prev = pages[block->phys_2 >> 12].block_2[(block->phys_2 >> 10) & 3];

        if (block_prev) {
            block->next_2                                                 = block_prev;
            block_prev->prev_2                                            = block;
            pages[block->phys_2 >> 12].block_2[(block->phys_2 >> 10) & 3] = block;
        } else {
            block->next_2                                                 = NULL;
            pages[block->phys_2 >> 12].block_2[(block->phys_2 >> 10) & 3] = block;
        }
    }
}

static void
remove_from_block_list(codeblock_t *block, uint32_t pc)
{
    if (!block->page_mask)
        return;

    if (block->prev) {
        block->prev->next = block->next;
        if (block->next)
            block->next->prev = block->prev;
    } else {
        pages[block->phys >> 12].block[(block->phys >> 10) & 3] = block->next;
        if (block->next)
            block->next->prev = NULL;
        else
            mem_flush_write_page(block->phys, 0);
    }
    if (!block->page_mask2) {
        if (block->prev_2 || block->next_2)
            fatal("Invalid block_2\n");
        return;
    }

    if (block->prev_2) {
        block->prev_2->next_2 = block->next_2;
        if (block->next_2)
            block->next_2->prev_2 = block->prev_2;
    } else {
        pages[block->phys_2 >> 12].block_2[(block->phys_2 >> 10) & 3] = block->next_2;
        if (block->next_2)
            block->next_2->prev_2 = NULL;
        else
            mem_flush_write_page(block->phys_2, 0);
    }
}

static void
delete_block(codeblock_t *block)
{
    uint32_t old_pc = block->pc;

    if (block == codeblock_hash[HASH(block->phys)])
        codeblock_hash[HASH(block->phys)] = NULL;

    if (block->valid == 0)
        fatal("Deleting deleted block\n");
    block->valid = 0;

    codeblock_tree_delete(block);
    remove_from_block_list(block, old_pc);
}

void
codegen_check_flush(page_t *page, uint64_t mask, uint32_t phys_addr)
{
    struct codeblock_t *block = page->block[(phys_addr >> 10) & 3];

    while (block) {
        if (mask & block->page_mask) {
            delete_block(block);
        }
        if (block == block->next)
            fatal("Broken 1\n");
        block = block->next;
    }

    block = page->block_2[(phys_addr >> 10) & 3];

    while (block) {
        if (mask & block->page_mask2) {
            delete_block(block);
        }
        if (block == block->next_2)
            fatal("Broken 2\n");
        block = block->next_2;
    }
}

void
codegen_block_init(uint32_t phys_addr)
{
    codeblock_t *block;
    page_t      *page = &pages[phys_addr >> 12];

    if (!page->block[(phys_addr >> 10) & 3])
        mem_flush_write_page(phys_addr, cs + cpu_state.pc);

    block_current = (block_current + 1) & BLOCK_MASK;
    block         = &codeblock[block_current];

    if (block->valid != 0) {
        delete_block(block);
    }
    block_num                 = HASH(phys_addr);
    codeblock_hash[block_num] = &codeblock[block_current];

    block->valid       = 1;
    block->ins         = 0;
    block->pc          = cs + cpu_state.pc;
    block->_cs         = cs;
    block->pnt         = block_current;
    block->phys        = phys_addr;
    block->dirty_mask  = &page->dirty_mask[(phys_addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK];
    block->dirty_mask2 = NULL;
    block->next = block->prev = NULL;
    block->next_2 = block->prev_2 = NULL;
    block->page_mask              = 0;
    block->flags                  = 0;
    block->status                 = cpu_cur_status;

    block->was_recompiled = 0;

    recomp_page = block->phys & ~0xfff;

    codeblock_tree_add(block);
}

void
codegen_block_start_recompile(codeblock_t *block)
{
    page_t   *page = &pages[block->phys >> 12];
    uintptr_t rip_rel;

    if (!page->block[(block->phys >> 10) & 3])
        mem_flush_write_page(block->phys, cs + cpu_state.pc);

    block_num     = HASH(block->phys);
    block_current = block->pnt;

    if (block->pc != cs + cpu_state.pc || block->was_recompiled)
        fatal("Recompile to used block!\n");

    block->status = cpu_cur_status;

    block_pos = BLOCK_GPF_OFFSET;
#    ifdef OLD_GPF
#        if _WIN64
    addbyte(0x48); /*XOR RCX, RCX*/
    addbyte(0x31);
    addbyte(0xc9);
    addbyte(0x31); /*XOR EDX, EDX*/
    addbyte(0xd2);
#        else
    addbyte(0x48); /*XOR RDI, RDI*/
    addbyte(0x31);
    addbyte(0xff);
    addbyte(0x31); /*XOR ESI, ESI*/
    addbyte(0xf6);
#        endif
    call(block, (uintptr_t) x86gpf);
    while (block_pos < BLOCK_EXIT_OFFSET)
        addbyte(0x90); /*NOP*/
#    else
    addbyte(0xC6); /*MOVB ABRT_GPF,(abrt)*/
    addbyte(0x45);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(ABRT_GPF);
    addbyte(0x31); /* xor eax,eax */
    addbyte(0xc0);
    addbyte(0x89); /*MOVB eax,(abrt_error)*/
    addbyte(0x85);
    rip_rel = ((uintptr_t) &cpu_state) + 128;
    rip_rel = ((uintptr_t) & (abrt_error)) - rip_rel;
    addlong((uint32_t) rip_rel);
#    endif
    block_pos = BLOCK_EXIT_OFFSET; /*Exit code*/
    addbyte(0x48);                 /*ADDL $40,%rsp*/
    addbyte(0x83);
    addbyte(0xC4);
    addbyte(0x28);
    addbyte(0x41); /*POP R15*/
    addbyte(0x5f);
    addbyte(0x41); /*POP R14*/
    addbyte(0x5e);
    addbyte(0x41); /*POP R13*/
    addbyte(0x5d);
    addbyte(0x41); /*POP R12*/
    addbyte(0x5c);
    addbyte(0x5f); /*POP RDI*/
    addbyte(0x5e); /*POP RSI*/
    addbyte(0x5d); /*POP RBP*/
    addbyte(0x5b); /*POP RDX*/
    addbyte(0xC3); /*RET*/
    cpu_block_end = 0;
    block_pos     = 0; /*Entry code*/
    addbyte(0x53);     /*PUSH RBX*/
    addbyte(0x55);     /*PUSH RBP*/
    addbyte(0x56);     /*PUSH RSI*/
    addbyte(0x57);     /*PUSH RDI*/
    addbyte(0x41);     /*PUSH R12*/
    addbyte(0x54);
    addbyte(0x41); /*PUSH R13*/
    addbyte(0x55);
    addbyte(0x41); /*PUSH R14*/
    addbyte(0x56);
    addbyte(0x41); /*PUSH R15*/
    addbyte(0x57);
    addbyte(0x48); /*SUBL $40,%rsp*/
    addbyte(0x83);
    addbyte(0xEC);
    addbyte(0x28);
    addbyte(0x48); /*MOVL RBP, &cpu_state*/
    addbyte(0xBD);
    addquad(((uintptr_t) &cpu_state) + 128);

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

    codegen_fpu_loaded_iq[0] = codegen_fpu_loaded_iq[1] = codegen_fpu_loaded_iq[2] = codegen_fpu_loaded_iq[3] = codegen_fpu_loaded_iq[4] = codegen_fpu_loaded_iq[5] = codegen_fpu_loaded_iq[6] = codegen_fpu_loaded_iq[7] = 0;

    cpu_state.seg_ds.checked = cpu_state.seg_es.checked = cpu_state.seg_fs.checked = cpu_state.seg_gs.checked = (cr0 & 1) ? 0 : 1;

    codegen_reg_loaded[0] = codegen_reg_loaded[1] = codegen_reg_loaded[2] = codegen_reg_loaded[3] = codegen_reg_loaded[4] = codegen_reg_loaded[5] = codegen_reg_loaded[6] = codegen_reg_loaded[7] = 0;

    block->was_recompiled = 1;

    codegen_flat_ds = !(cpu_cur_status & CPU_STATUS_NOTFLATDS);
    codegen_flat_ss = !(cpu_cur_status & CPU_STATUS_NOTFLATSS);
}

void
codegen_block_remove(void)
{
    codeblock_t *block = &codeblock[block_current];

    delete_block(block);

    recomp_page = -1;
}

void
codegen_block_generate_end_mask(void)
{
    codeblock_t *block = &codeblock[block_current];
    uint32_t     start_pc;
    uint32_t     end_pc;

    block->endpc = codegen_endpc;

    block->page_mask = 0;
    start_pc         = (block->pc & 0x3ff) & ~15;
    if ((block->pc ^ block->endpc) & ~0x3ff)
        end_pc = 0x3ff & ~15;
    else
        end_pc = (block->endpc & 0x3ff) & ~15;
    if (end_pc < start_pc)
        end_pc = 0x3ff;
    start_pc >>= PAGE_MASK_SHIFT;
    end_pc >>= PAGE_MASK_SHIFT;

    for (; start_pc <= end_pc; start_pc++)
        block->page_mask |= ((uint64_t) 1 << start_pc);

    pages[block->phys >> 12].code_present_mask[(block->phys >> 10) & 3] |= block->page_mask;

    block->phys_2     = -1;
    block->page_mask2 = 0;
    block->next_2 = block->prev_2 = NULL;
    if ((block->pc ^ block->endpc) & ~0x3ff) {
        block->phys_2 = get_phys_noabrt(block->endpc);
        if (block->phys_2 != -1) {
            page_t *page_2 = &pages[block->phys_2 >> 12];

            start_pc = 0;
            end_pc   = (block->endpc & 0x3ff) >> PAGE_MASK_SHIFT;
            for (; start_pc <= end_pc; start_pc++)
                block->page_mask2 |= ((uint64_t) 1 << start_pc);
            page_2->code_present_mask[(block->phys_2 >> 10) & 3] |= block->page_mask2;

            if (!pages[block->phys_2 >> 12].block_2[(block->phys_2 >> 10) & 3])
                mem_flush_write_page(block->phys_2, block->endpc);

            if (!block->page_mask2)
                fatal("!page_mask2\n");
            if (block->next_2) {
                if (block->next_2->valid == 0)
                    fatal("block->next_2->valid=0 %p\n", (void *) block->next_2);
            }

            block->dirty_mask2 = &page_2->dirty_mask[(block->phys_2 >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK];
        }
    }

    recomp_page = -1;
}

void
codegen_block_end(void)
{
    codeblock_t *block = &codeblock[block_current];

    codegen_block_generate_end_mask();
    add_to_block_list(block);
}

void
codegen_block_end_recompile(codeblock_t *block)
{
    codegen_timing_block_end();
    codegen_accumulate(ACCREG_cycles, -codegen_block_cycles);

    codegen_accumulate_flush();

    addbyte(0x48); /*ADDL $40,%rsp*/
    addbyte(0x83);
    addbyte(0xC4);
    addbyte(0x28);
    addbyte(0x41); /*POP R15*/
    addbyte(0x5f);
    addbyte(0x41); /*POP R14*/
    addbyte(0x5e);
    addbyte(0x41); /*POP R13*/
    addbyte(0x5d);
    addbyte(0x41); /*POP R12*/
    addbyte(0x5c);
    addbyte(0x5f); /*POP RDI*/
    addbyte(0x5e); /*POP RSI*/
    addbyte(0x5d); /*POP RBP*/
    addbyte(0x5b); /*POP RDX*/
    addbyte(0xC3); /*RET*/

    if (block_pos > BLOCK_GPF_OFFSET)
        fatal("Over limit!\n");

    remove_from_block_list(block, block->pc);
    block->next = block->prev = NULL;
    block->next_2 = block->prev_2 = NULL;
    codegen_block_generate_end_mask();
    add_to_block_list(block);
}

void
codegen_flush(void)
{
    return;
}

// clang-format off
static int opcode_modrm[256] = {
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0, /*00*/
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0, /*10*/
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0, /*20*/
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0, /*30*/

    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*40*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*50*/
    0, 0, 1, 1,  0, 0, 0, 0,  0, 1, 0, 1,  0, 0, 0, 0, /*60*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*70*/

    1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, /*80*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*90*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*a0*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*b0*/

    1, 1, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0,  0, 0, 0, 0, /*c0*/
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  1, 1, 1, 1, /*d0*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*e0*/
    0, 0, 0, 0,  0, 0, 1, 1,  0, 0, 0, 0,  0, 0, 1, 1, /*f0*/
};

int opcode_0f_modrm[256] = {
    1, 1, 1, 1,  0, 0, 0, 0,  0, 0, 0, 0,  0, 1, 0, 1, /*00*/
    0, 0, 0, 0,  0, 0, 0, 0,  1, 1, 1, 1,  1, 1, 1, 1, /*10*/
    1, 1, 1, 1,  1, 1, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*20*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 1, /*30*/

    1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, /*40*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*50*/
    1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  0, 0, 1, 1, /*60*/
    0, 1, 1, 1,  1, 1, 1, 0,  0, 0, 0, 0,  0, 0, 1, 1, /*70*/

    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*80*/
    1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, /*90*/
    0, 0, 0, 1,  1, 1, 0, 0,  0, 0, 0, 1,  1, 1, 1, 1, /*a0*/
    1, 1, 1, 1,  1, 1, 1, 1,  0, 0, 1, 1,  1, 1, 1, 1, /*b0*/

    1, 1, 0, 0,  0, 0, 0, 1,  0, 0, 0, 0,  0, 0, 0, 0, /*c0*/
    0, 1, 1, 1,  0, 1, 0, 0,  1, 1, 0, 1,  1, 1, 0, 1, /*d0*/
    0, 1, 1, 0,  0, 1, 0, 0,  1, 1, 0, 1,  1, 1, 0, 1, /*e0*/
    0, 1, 1, 1,  0, 1, 0, 0,  1, 1, 1, 0,  1, 1, 1, 0  /*f0*/
};
// clang-format off

void
codegen_debug(void)
{
}

static x86seg *
codegen_generate_ea_16_long(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc)
{
    if (!cpu_mod && cpu_rm == 6) {
        addbyte(0xC7); /*MOVL $0,(ssegs)*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(eaaddr));
        addlong((fetchdat >> 8) & 0xffff);
        (*op_pc) += 2;
    } else {
        int base_reg = 0, index_reg = 0;

        switch (cpu_rm) {
            case 0:
            case 1:
            case 7:
                base_reg = LOAD_REG_W(REG_BX);
                break;
            case 2:
            case 3:
            case 6:
                base_reg = LOAD_REG_W(REG_BP);
                break;
            case 4:
                base_reg = LOAD_REG_W(REG_SI);
                break;
            case 5:
                base_reg = LOAD_REG_W(REG_DI);
                break;
        }
        if (!(cpu_rm & 4)) {
            if (cpu_rm & 1)
                index_reg = LOAD_REG_W(REG_DI);
            else
                index_reg = LOAD_REG_W(REG_SI);
        }
        base_reg &= 7;
        index_reg &= 7;

        switch (cpu_mod) {
            case 0:
                if (cpu_rm & 4) {
                    addbyte(0x41); /*MOVZX EAX, base_reg*/
                    addbyte(0x0f);
                    addbyte(0xb7);
                    addbyte(0xc0 | base_reg);
                } else {
                    addbyte(0x67); /*LEA EAX, base_reg+index_reg*/
                    addbyte(0x43);
                    addbyte(0x8d);
                    if (base_reg == 5) {
                        addbyte(0x44);
                        addbyte(base_reg | (index_reg << 3));
                        addbyte(0);
                    } else {
                        addbyte(0x04);
                        addbyte(base_reg | (index_reg << 3));
                    }
                }
                break;
            case 1:
                if (cpu_rm & 4) {
                    addbyte(0x67); /*LEA EAX, base_reg+imm8*/
                    addbyte(0x41);
                    addbyte(0x8d);
                    addbyte(0x40 | base_reg);
                    addbyte((fetchdat >> 8) & 0xff);
                } else {
                    addbyte(0x67); /*LEA EAX, base_reg+index_reg+imm8*/
                    addbyte(0x43);
                    addbyte(0x8d);
                    addbyte(0x44);
                    addbyte(base_reg | (index_reg << 3));
                    addbyte((fetchdat >> 8) & 0xff);
                }
                (*op_pc)++;
                break;
            case 2:
                if (cpu_rm & 4) {
                    addbyte(0x67); /*LEA EAX, base_reg+imm8*/
                    addbyte(0x41);
                    addbyte(0x8d);
                    addbyte(0x80 | base_reg);
                    addlong((fetchdat >> 8) & 0xffff);
                } else {
                    addbyte(0x67); /*LEA EAX, base_reg+index_reg+imm16*/
                    addbyte(0x43);
                    addbyte(0x8d);
                    addbyte(0x84);
                    addbyte(base_reg | (index_reg << 3));
                    addlong((fetchdat >> 8) & 0xffff);
                }
                (*op_pc) += 2;
                break;
        }
        if (cpu_mod || !(cpu_rm & 4)) {
            addbyte(0x25); /*ANDL $0xffff, %eax*/
            addlong(0xffff);
        }
        addbyte(0x89); /*MOV eaaddr, EAX*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(eaaddr));

        if (mod1seg[cpu_rm] == &ss && !op_ssegs)
            op_ea_seg = &cpu_state.seg_ss;
    }
    return op_ea_seg;
}
// #if 0
static x86seg *
codegen_generate_ea_32_long(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, int stack_offset)
{
    uint32_t new_eaaddr;

    if (cpu_rm == 4) {
        uint8_t sib      = fetchdat >> 8;
        int     base_reg = -1, index_reg = -1;

        (*op_pc)++;

        if (cpu_mod || (sib & 7) != 5)
            base_reg = LOAD_REG_L(sib & 7) & 7;

        if (((sib >> 3) & 7) != 4)
            index_reg = LOAD_REG_L((sib >> 3) & 7) & 7;

        if (index_reg == -1) {
            switch (cpu_mod) {
                case 0:
                    if ((sib & 7) == 5) {
                        new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                        addbyte(0xb8); /*MOV EAX, imm32*/
                        addlong(new_eaaddr);
                        (*op_pc) += 4;
                    } else {
                        addbyte(0x44); /*MOV EAX, base_reg*/
                        addbyte(0x89);
                        addbyte(0xc0 | (base_reg << 3));
                    }
                    break;
                case 1:
                    addbyte(0x67); /*LEA EAX, imm8+base_reg*/
                    addbyte(0x41);
                    addbyte(0x8d);
                    if (base_reg == 4) {
                        addbyte(0x44);
                        addbyte(0x24);
                    } else {
                        addbyte(0x40 | base_reg);
                    }
                    addbyte((fetchdat >> 16) & 0xff);
                    (*op_pc)++;
                    break;
                case 2:
                    new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                    addbyte(0x67); /*LEA EAX, imm32+base_reg*/
                    addbyte(0x41);
                    addbyte(0x8d);
                    if (base_reg == 4) {
                        addbyte(0x84);
                        addbyte(0x24);
                    } else {
                        addbyte(0x80 | base_reg);
                    }
                    addlong(new_eaaddr);
                    (*op_pc) += 4;
                    break;
            }
        } else {
            switch (cpu_mod) {
                case 0:
                    if ((sib & 7) == 5) {
                        new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                        if (sib >> 6) {
                            addbyte(0x67); /*LEA EAX, imm32+index_reg*scale*/
                            addbyte(0x42);
                            addbyte(0x8d);
                            addbyte(0x04);
                            addbyte(0x05 | (sib & 0xc0) | (index_reg << 3));
                            addlong(new_eaaddr);
                        } else {
                            addbyte(0x67); /*LEA EAX, imm32+index_reg*/
                            addbyte(0x41);
                            addbyte(0x8d);
                            addbyte(0x80 | index_reg);
                            addlong(new_eaaddr);
                        }
                        (*op_pc) += 4;
                    } else {
                        addbyte(0x67); /*LEA EAX, base_reg+index_reg*scale*/
                        addbyte(0x43);
                        addbyte(0x8d);
                        if (base_reg == 5) {
                            addbyte(0x44);
                            addbyte(base_reg | (index_reg << 3) | (sib & 0xc0));
                            addbyte(0);
                        } else {
                            addbyte(0x04);
                            addbyte(base_reg | (index_reg << 3) | (sib & 0xc0));
                        }
                    }
                    break;
                case 1:
                    addbyte(0x67); /*LEA EAX, imm8+base_reg+index_reg*scale*/
                    addbyte(0x43);
                    addbyte(0x8d);
                    addbyte(0x44);
                    addbyte(base_reg | (index_reg << 3) | (sib & 0xc0));
                    addbyte((fetchdat >> 16) & 0xff);
                    (*op_pc)++;
                    break;
                case 2:
                    new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                    addbyte(0x67); /*LEA EAX, imm32+base_reg+index_reg*scale*/
                    addbyte(0x43);
                    addbyte(0x8d);
                    addbyte(0x84);
                    addbyte(base_reg | (index_reg << 3) | (sib & 0xc0));
                    addlong(new_eaaddr);
                    (*op_pc) += 4;
                    break;
            }
        }
        if (stack_offset && (sib & 7) == 4 && (cpu_mod || (sib & 7) != 5)) /*ESP*/
        {
            addbyte(0x05);
            addlong(stack_offset);
        }
        if (((sib & 7) == 4 || (cpu_mod && (sib & 7) == 5)) && !op_ssegs)
            op_ea_seg = &cpu_state.seg_ss;

        addbyte(0x89); /*MOV eaaddr, EAX*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(eaaddr));
    } else {
        int base_reg;

        if (!cpu_mod && cpu_rm == 5) {
            new_eaaddr = fastreadl(cs + (*op_pc) + 1);
            addbyte(0xC7); /*MOVL $new_eaaddr,(eaaddr)*/
            addbyte(0x45);
            addbyte((uint8_t) cpu_state_offset(eaaddr));
            addlong(new_eaaddr);
            (*op_pc) += 4;
            return op_ea_seg;
        }
        base_reg = LOAD_REG_L(cpu_rm) & 7;
        if (cpu_mod) {
            if (cpu_rm == 5 && !op_ssegs)
                op_ea_seg = &cpu_state.seg_ss;
            if (cpu_mod == 1) {
                addbyte(0x67); /*LEA EAX, base_reg+imm8*/
                addbyte(0x41);
                addbyte(0x8d);
                addbyte(0x40 | base_reg);
                addbyte((fetchdat >> 8) & 0xff);
                (*op_pc)++;
            } else {
                new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                addbyte(0x67); /*LEA EAX, base_reg+imm32*/
                addbyte(0x41);
                addbyte(0x8d);
                addbyte(0x80 | base_reg);
                addlong(new_eaaddr);
                (*op_pc) += 4;
            }
            addbyte(0x89); /*MOV eaaddr, EAX*/
            addbyte(0x45);
            addbyte((uint8_t) cpu_state_offset(eaaddr));
        } else {
            addbyte(0x44); /*MOV eaaddr, base_reg*/
            addbyte(0x89);
            addbyte(0x45 | (base_reg << 3));
            addbyte((uint8_t) cpu_state_offset(eaaddr));
        }
    }
    return op_ea_seg;
}
// #endif
void
codegen_generate_call(uint8_t opcode, OpFn op, uint32_t fetchdat, uint32_t new_pc, uint32_t old_pc)
{
    codeblock_t *block           = &codeblock[block_current];
    uint32_t     op_32           = use32;
    uint32_t     op_pc           = new_pc;
    const OpFn  *op_table        = (OpFn *) x86_dynarec_opcodes;
    RecompOpFn  *recomp_op_table = recomp_opcodes;
    int          opcode_shift    = 0;
    int          opcode_mask     = 0x3ff;
    int          over            = 0;
    int          pc_off          = 0;
    int          test_modrm      = 1;
    int          c;

    op_ea_seg = &cpu_state.seg_ds;
    op_ssegs  = 0;
    op_old_pc = old_pc;

    for (c = 0; c < NR_HOST_REGS; c++)
        host_reg_mapping[c] = -1;
    for (c = 0; c < NR_HOST_XMM_REGS; c++)
        host_reg_xmm_mapping[c] = -1;

    codegen_timing_start();

    while (!over) {
        switch (opcode) {
            case 0x0f:
                op_table        = x86_dynarec_opcodes_0f;
                recomp_op_table = recomp_opcodes_0f;
                over            = 1;
                break;

            case 0x26: /*ES:*/
                op_ea_seg = &cpu_state.seg_es;
                op_ssegs  = 1;
                break;
            case 0x2e: /*CS:*/
                op_ea_seg = &cpu_state.seg_cs;
                op_ssegs  = 1;
                break;
            case 0x36: /*SS:*/
                op_ea_seg = &cpu_state.seg_ss;
                op_ssegs  = 1;
                break;
            case 0x3e: /*DS:*/
                op_ea_seg = &cpu_state.seg_ds;
                op_ssegs  = 1;
                break;
            case 0x64: /*FS:*/
                op_ea_seg = &cpu_state.seg_fs;
                op_ssegs  = 1;
                break;
            case 0x65: /*GS:*/
                op_ea_seg = &cpu_state.seg_gs;
                op_ssegs  = 1;
                break;

            case 0x66: /*Data size select*/
                op_32 = ((use32 & 0x100) ^ 0x100) | (op_32 & 0x200);
                break;
            case 0x67: /*Address size select*/
                op_32 = ((use32 & 0x200) ^ 0x200) | (op_32 & 0x100);
                break;

            case 0xd8:
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_d8_a32 : x86_dynarec_opcodes_d8_a16;
                recomp_op_table = fpu_softfloat ? recomp_opcodes_NULL : recomp_opcodes_d8;
                opcode_shift    = 3;
                opcode_mask     = 0x1f;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xd9:
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_d9_a32 : x86_dynarec_opcodes_d9_a16;
                recomp_op_table = fpu_softfloat ? recomp_opcodes_NULL : recomp_opcodes_d9;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xda:
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_da_a32 : x86_dynarec_opcodes_da_a16;
                recomp_op_table = fpu_softfloat ? recomp_opcodes_NULL : recomp_opcodes_da;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xdb:
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_db_a32 : x86_dynarec_opcodes_db_a16;
                recomp_op_table = fpu_softfloat ? recomp_opcodes_NULL : recomp_opcodes_db;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xdc:
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_dc_a32 : x86_dynarec_opcodes_dc_a16;
                recomp_op_table = fpu_softfloat ? recomp_opcodes_NULL : recomp_opcodes_dc;
                opcode_shift    = 3;
                opcode_mask     = 0x1f;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xdd:
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_dd_a32 : x86_dynarec_opcodes_dd_a16;
                recomp_op_table = fpu_softfloat ? recomp_opcodes_NULL : recomp_opcodes_dd;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xde:
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_de_a32 : x86_dynarec_opcodes_de_a16;
                recomp_op_table = fpu_softfloat ? recomp_opcodes_NULL : recomp_opcodes_de;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xdf:
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_df_a32 : x86_dynarec_opcodes_df_a16;
                recomp_op_table = fpu_softfloat ? recomp_opcodes_NULL : recomp_opcodes_df;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;

            case 0xf0: /*LOCK*/
                break;

            case 0xf2: /*REPNE*/
                op_table        = x86_dynarec_opcodes_REPNE;
                recomp_op_table = recomp_opcodes_REPNE;
                break;
            case 0xf3: /*REPE*/
                op_table        = x86_dynarec_opcodes_REPE;
                recomp_op_table = recomp_opcodes_REPE;
                break;

            default:
                goto generate_call;
        }
        fetchdat = fastreadl(cs + op_pc);
        codegen_timing_prefix(opcode, fetchdat);
        if (cpu_state.abrt)
            return;
        opcode = fetchdat & 0xff;
        if (!pc_off)
            fetchdat >>= 8;
        op_pc++;
    }

generate_call:
    codegen_timing_opcode(opcode, fetchdat, op_32, op_pc);

    codegen_accumulate(ACCREG_cycles, -codegen_block_cycles);
    codegen_block_cycles = 0;

    if ((op_table == x86_dynarec_opcodes && ((opcode & 0xf0) == 0x70 || (opcode & 0xfc) == 0xe0 || opcode == 0xc2 || (opcode & 0xfe) == 0xca || (opcode & 0xfc) == 0xcc || (opcode & 0xfc) == 0xe8 || (opcode == 0xff && ((fetchdat & 0x38) >= 0x10 && (fetchdat & 0x38) < 0x30)))) || (op_table == x86_dynarec_opcodes_0f && ((opcode & 0xf0) == 0x80))) {
        /*On some CPUs (eg K6), a jump/branch instruction may be able to pair with
          subsequent instructions, so no cycles may have been deducted for it yet.
          To prevent having zero cycle blocks (eg with a jump instruction pointing
          to itself), apply the cycles that would be taken if this jump is taken,
          then reverse it for subsequent instructions if the jump is not taken*/
        int jump_cycles = 0;

        if (codegen_timing_jump_cycles != NULL)
            jump_cycles = codegen_timing_jump_cycles();

        if (jump_cycles)
            codegen_accumulate(ACCREG_cycles, -jump_cycles);
        codegen_accumulate_flush();
        if (jump_cycles)
            codegen_accumulate(ACCREG_cycles, jump_cycles);
    }

    if ((op_table == x86_dynarec_opcodes_REPNE || op_table == x86_dynarec_opcodes_REPE) && !op_table[opcode | op_32]) {
        op_table        = x86_dynarec_opcodes;
        recomp_op_table = recomp_opcodes;
    }

    if (recomp_op_table && recomp_op_table[(opcode | op_32) & 0x1ff]) {
        uint32_t new_pc = recomp_op_table[(opcode | op_32) & 0x1ff](opcode, fetchdat, op_32, op_pc, block);
        if (new_pc) {
            if (new_pc != -1)
                STORE_IMM_ADDR_L((uintptr_t) &cpu_state.pc, new_pc);

            codegen_block_ins++;
            block->ins++;
            codegen_block_full_ins++;
            codegen_endpc = (cs + cpu_state.pc) + 8;

#    ifdef CHECK_INT
            /* Check for interrupts. */
            addbyte(0xf6); /* test byte ptr[&pic_pending],1 */
            addbyte(0x04);
            addbyte(0x25);
            addlong((uint32_t) (uintptr_t) &pic_pending);
            addbyte(0x01);
            addbyte(0x0F);
            addbyte(0x85); /*JNZ 0*/
            addlong((uint32_t) (uintptr_t) &block->data[BLOCK_EXIT_OFFSET] - (uint32_t) (uintptr_t) (&block->data[block_pos + 4]));
#    endif

            return;
        }
    }

    op = op_table[((opcode >> opcode_shift) | op_32) & opcode_mask];
    if (op_ssegs != last_ssegs) {
        last_ssegs = op_ssegs;
        addbyte(0xC6); /*MOVB $0,(ssegs)*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(ssegs));
        addbyte(op_ssegs);
    }
    if ((!test_modrm || (op_table == x86_dynarec_opcodes && opcode_modrm[opcode]) || (op_table == x86_dynarec_opcodes_0f && opcode_0f_modrm[opcode])) /* && !(op_32 & 0x200)*/) {
        int stack_offset = 0;

        if (op_table == x86_dynarec_opcodes && opcode == 0x8f) /*POP*/
            stack_offset = (op_32 & 0x100) ? 4 : 2;

        cpu_mod = (fetchdat >> 6) & 3;
        cpu_reg = (fetchdat >> 3) & 7;
        cpu_rm  = fetchdat & 7;

        addbyte(0xC7); /*MOVL $rm | mod | reg,(rm_mod_reg_data)*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(rm_data.rm_mod_reg_data));
        addlong(cpu_rm | (cpu_mod << 8) | (cpu_reg << 16));

        op_pc += pc_off;
        if (cpu_mod != 3 && !(op_32 & 0x200))
            op_ea_seg = codegen_generate_ea_16_long(op_ea_seg, fetchdat, op_ssegs, &op_pc);
        if (cpu_mod != 3 && (op_32 & 0x200))
            op_ea_seg = codegen_generate_ea_32_long(op_ea_seg, fetchdat, op_ssegs, &op_pc, stack_offset);
        op_pc -= pc_off;
    }
    if (op_ea_seg != last_ea_seg) {
        addbyte(0xC7); /*MOVL $&_ds,(ea_seg)*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(ea_seg));
        addlong((uint32_t) (uintptr_t) op_ea_seg);
    }

    codegen_accumulate_flush();

    addbyte(0xC7); /*MOVL [pc],new_pc*/
    addbyte(0x45);
    addbyte((uint8_t) cpu_state_offset(pc));
    addlong(op_pc + pc_off);
    addbyte(0xC7); /*MOVL $old_pc,(oldpc)*/
    addbyte(0x45);
    addbyte((uint8_t) cpu_state_offset(oldpc));
    addlong(old_pc);
    if (op_32 != last_op32) {
        last_op32 = op_32;
        addbyte(0xC7); /*MOVL $use32,(op32)*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(op32));
        addlong(op_32);
    }

    load_param_1_32(block, fetchdat);
    call(block, (uintptr_t) op);

    codegen_block_ins++;

    block->ins++;

#    ifdef CHECK_INT
    /* Check for interrupts. */
    addbyte(0x0a); /* or  al,byte ptr[&pic_pending] */
    addbyte(0x04);
    addbyte(0x25);
    addlong((uint32_t) (uintptr_t) &pic_pending);
#    endif

    addbyte(0x85); /*OR %eax, %eax*/
    addbyte(0xc0);
    addbyte(0x0F);
    addbyte(0x85); /*JNZ 0*/
    addlong((uint32_t) (uintptr_t) &block->data[BLOCK_EXIT_OFFSET] - (uint32_t) (uintptr_t) (&block->data[block_pos + 4]));

    codegen_endpc = (cs + cpu_state.pc) + 8;
}

#endif
