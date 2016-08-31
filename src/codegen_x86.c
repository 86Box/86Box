#if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined WIN32 || defined _WIN32 || defined _WIN32

#include <stdlib.h>
#include "ibm.h"
#include "cpu.h"
#include "x86.h"
#include "x86_flags.h"
#include "x86_ops.h"
#include "x87.h"
#include "mem.h"

#include "386_common.h"

#include "codegen.h"
#include "codegen_ops.h"
#include "codegen_ops_x86.h"

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif
#if defined WIN32 || defined _WIN32 || defined _WIN32
#include <windows.h>
#endif

int mmx_ebx_ecx_loaded;
int codegen_flags_changed = 0;
int codegen_fpu_entered = 0;
int codegen_mmx_entered = 0;
int codegen_fpu_loaded_iq[8];
x86seg *op_ea_seg;
int op_ssegs;
uint32_t op_old_pc;

uint32_t recomp_page = -1;

int host_reg_mapping[NR_HOST_REGS];
int host_reg_xmm_mapping[NR_HOST_XMM_REGS];
codeblock_t *codeblock;
codeblock_t **codeblock_hash;


int block_current = 0;
static int block_num;
int block_pos;

int cpu_recomp_flushes, cpu_recomp_flushes_latched;
int cpu_recomp_evicted, cpu_recomp_evicted_latched;
int cpu_recomp_reuse, cpu_recomp_reuse_latched;
int cpu_recomp_removed, cpu_recomp_removed_latched;

uint32_t codegen_endpc;

int codegen_block_cycles;
static int codegen_block_ins;
static int codegen_block_full_ins;

static uint32_t last_op32;
static x86seg *last_ea_seg;
static int last_ssegs;

void codegen_init()
{
        int c;
#ifdef __linux__
	void *start;
	size_t len;
	long pagesize = sysconf(_SC_PAGESIZE);
	long pagemask = ~(pagesize - 1);
#endif
        
#if defined WIN32 || defined _WIN32 || defined _WIN32
        codeblock = VirtualAlloc(NULL, BLOCK_SIZE * sizeof(codeblock_t), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
        codeblock = malloc(BLOCK_SIZE * sizeof(codeblock_t));
#endif
        codeblock_hash = malloc(HASH_SIZE * sizeof(codeblock_t *));

        memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
        memset(codeblock_hash, 0, HASH_SIZE * sizeof(codeblock_t *));

#ifdef __linux__
	start = (void *)((long)codeblock & pagemask);
	len = ((BLOCK_SIZE * sizeof(codeblock_t)) + pagesize) & pagemask;
	if (mprotect(start, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
	{
		perror("mprotect");
		exit(-1);
	}
#endif
//        pclog("Codegen is %p\n", (void *)pages[0xfab12 >> 12].block);
}

void codegen_reset()
{
        memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
        memset(codeblock_hash, 0, HASH_SIZE * sizeof(codeblock_t *));
        mem_reset_page_blocks();
}

void dump_block()
{
        codeblock_t *block = pages[0x119000 >> 12].block;

        pclog("dump_block:\n");
        while (block)
        {
                uint32_t start_pc = (block->pc & 0xffc) | (block->phys & ~0xfff);
                uint32_t end_pc = (block->endpc & 0xffc) | (block->phys & ~0xfff);
                pclog(" %p : %08x-%08x  %08x-%08x %p %p\n", (void *)block, start_pc, end_pc,  block->pc, block->endpc, (void *)block->prev, (void *)block->next);
                if (!block->pc)
                        fatal("Dead PC=0\n");
                
                block = block->next;
        }
        pclog("dump_block done\n");
}

static void add_to_block_list(codeblock_t *block)
{
        codeblock_t *block_prev = pages[block->phys >> 12].block;

        if (!block->page_mask)
                fatal("add_to_block_list - mask = 0\n");

        if (block_prev)
        {
                block->next = block_prev;
                block_prev->prev = block;
                pages[block->phys >> 12].block = block;
        }
        else
        {
                block->next = NULL;
                pages[block->phys >> 12].block = block;
        }

        if (block->next)
        {
                if (!block->next->pc)
                        fatal("block->next->pc=0 %p %p %x %x\n", (void *)block->next, (void *)codeblock, block_current, block_pos);
        }
        
        if (block->page_mask2)
        {
                block_prev = pages[block->phys_2 >> 12].block_2;

                if (block_prev)
                {
                        block->next_2 = block_prev;
                        block_prev->prev_2 = block;
                        pages[block->phys_2 >> 12].block_2 = block;
                }
                else
                {
                        block->next_2 = NULL;
                        pages[block->phys_2 >> 12].block_2 = block;
                }
        }
}

static void remove_from_block_list(codeblock_t *block, uint32_t pc)
{
        if (!block->page_mask)
                return;

        if (block->prev)
        {
                block->prev->next = block->next;
                if (block->next)
                        block->next->prev = block->prev;
        }
        else
        {
                pages[block->phys >> 12].block = block->next;
                if (block->next)
                        block->next->prev = NULL;
                else
                        mem_flush_write_page(block->phys, 0);
        }
        if (!block->page_mask2)
        {
                if (block->prev_2 || block->next_2)
                        fatal("Invalid block_2\n");
                return;
        }

        if (block->prev_2)
        {
                block->prev_2->next_2 = block->next_2;
                if (block->next_2)
                        block->next_2->prev_2 = block->prev_2;
        }
        else
        {
//                pclog(" pages.block_2=%p 3 %p %p\n", (void *)block->next_2, (void *)block, (void *)pages[block->phys_2 >> 12].block_2);
                pages[block->phys_2 >> 12].block_2 = block->next_2;
                if (block->next_2)
                        block->next_2->prev_2 = NULL;
                else
                        mem_flush_write_page(block->phys_2, 0);
        }
}

static void delete_block(codeblock_t *block)
{
        uint32_t old_pc = block->pc;

        if (block == codeblock_hash[HASH(block->phys)])
                codeblock_hash[HASH(block->phys)] = NULL;

        if (!block->pc)
                fatal("Deleting deleted block\n");
        block->pc = 0;

        codeblock_tree_delete(block);
        remove_from_block_list(block, old_pc);
}

void codegen_check_flush(page_t *page, uint64_t mask, uint32_t phys_addr)
{
        struct codeblock_t *block = page->block;

        while (block)
        {
                if (mask & block->page_mask)
                {
                        delete_block(block);
                        cpu_recomp_evicted++;
                }
                if (block == block->next)
                        fatal("Broken 1\n");
                block = block->next;
        }

        block = page->block_2;
        
        while (block)
        {
                if (mask & block->page_mask2)
                {
                        delete_block(block);
                        cpu_recomp_evicted++;
                }
                if (block == block->next_2)
                        fatal("Broken 2\n");
                block = block->next_2;
        }
}

void codegen_block_init(uint32_t phys_addr)
{
        codeblock_t *block;
        int has_evicted = 0;
        page_t *page = &pages[phys_addr >> 12];
        
        if (!page->block)
                mem_flush_write_page(phys_addr, cs+cpu_state.pc);

        block_current = (block_current + 1) & BLOCK_MASK;
        block = &codeblock[block_current];

//        if (block->pc == 0xb00b4ff5)
//                pclog("Init target block\n");
        if (block->pc != 0)
        {
//                pclog("Reuse block : was %08x now %08x\n", block->pc, cs+pc);
                delete_block(block);
                cpu_recomp_reuse++;
        }
        block_num = HASH(phys_addr);
        codeblock_hash[block_num] = &codeblock[block_current];

        block->ins = 0;
        block->pc = cs + cpu_state.pc;
        block->_cs = cs;
        block->pnt = block_current;
        block->phys = phys_addr;
        block->use32 = use32;
        block->stack32 = stack32;
        block->next = block->prev = NULL;
        block->next_2 = block->prev_2 = NULL;
        block->page_mask = 0;
        
        block->was_recompiled = 0;

        recomp_page = block->phys & ~0xfff;
        
        codeblock_tree_add(block);
}

void codegen_block_start_recompile(codeblock_t *block)
{
        int has_evicted = 0;
        page_t *page = &pages[block->phys >> 12];
        
        if (!page->block)
                mem_flush_write_page(block->phys, cs+cpu_state.pc);

        block_num = HASH(block->phys);
        block_current = block->pnt;

        if (block->pc != cs + cpu_state.pc || block->was_recompiled)
                fatal("Recompile to used block!\n");

        block_pos = BLOCK_GPF_OFFSET;
        addbyte(0xc7); /*MOV [ESP],0*/
        addbyte(0x04);
        addbyte(0x24);
        addlong(0);
        addbyte(0xc7); /*MOV [ESP+4],0*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x04);
        addlong(0);
        addbyte(0xe8); /*CALL x86gpf*/
        addlong((uint32_t)x86gpf - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        block_pos = BLOCK_EXIT_OFFSET; /*Exit code*/
        addbyte(0x83); /*ADDL $16,%esp*/
        addbyte(0xC4);
        addbyte(0x10);
        addbyte(0x5f); /*POP EDI*/
        addbyte(0x5e); /*POP ESI*/
        addbyte(0x5d); /*POP EBP*/
        addbyte(0x5b); /*POP EDX*/
        addbyte(0xC3); /*RET*/
        cpu_block_end = 0;
        block_pos = 0; /*Entry code*/
        addbyte(0x53); /*PUSH EBX*/
        addbyte(0x55); /*PUSH EBP*/
        addbyte(0x56); /*PUSH ESI*/
        addbyte(0x57); /*PUSH EDI*/
        addbyte(0x83); /*SUBL $16,%esp*/
        addbyte(0xEC);
        addbyte(0x10);
        addbyte(0xBD); /*MOVL EBP, &cpu_state*/
        addlong(((uintptr_t)&cpu_state) + 128);

//        pclog("New block %i for %08X   %03x\n", block_current, cs+pc, block_num);

        last_op32 = -1;
        last_ea_seg = NULL;
        last_ssegs = -1;
        
        codegen_block_cycles = 0;
        codegen_timing_block_start();
        
        codegen_block_ins = 0;
        codegen_block_full_ins = 0;

        recomp_page = block->phys & ~0xfff;
        
        codegen_flags_changed = 0;
        codegen_fpu_entered = 0;
        codegen_mmx_entered = 0;

        codegen_fpu_loaded_iq[0] = codegen_fpu_loaded_iq[1] = codegen_fpu_loaded_iq[2] = codegen_fpu_loaded_iq[3] =
        codegen_fpu_loaded_iq[4] = codegen_fpu_loaded_iq[5] = codegen_fpu_loaded_iq[6] = codegen_fpu_loaded_iq[7] = 0;
        
        _ds.checked = _es.checked = _fs.checked = _gs.checked = (cr0 & 1) ? 0 : 1;

        block->was_recompiled = 1;
}

void codegen_block_remove()
{
        codeblock_t *block = &codeblock[block_current];

        delete_block(block);
        cpu_recomp_removed++;

        recomp_page = -1;
}

void codegen_block_generate_end_mask()
{
        codeblock_t *block = &codeblock[block_current];
        uint32_t start_pc = (block->pc & 0xffc) | (block->phys & ~0xfff);
        uint32_t end_pc = ((codegen_endpc + 3) & 0xffc) | (block->phys & ~0xfff);

        block->endpc = codegen_endpc;

        block->page_mask = 0;
        start_pc = block->pc & 0xffc;
        start_pc &= ~PAGE_MASK_MASK;
        end_pc = ((block->endpc & 0xffc) + PAGE_MASK_MASK) & ~PAGE_MASK_MASK;
        if (end_pc > 0xfff || end_pc < start_pc)
                end_pc = 0xfff;
        start_pc >>= PAGE_MASK_SHIFT;
        end_pc >>= PAGE_MASK_SHIFT;
        
//        pclog("block_end: %08x %08x\n", start_pc, end_pc);
        for (; start_pc <= end_pc; start_pc++)
        {                
                block->page_mask |= ((uint64_t)1 << start_pc);
//                pclog("  %08x %llx\n", start_pc, block->page_mask);
        }
        
        pages[block->phys >> 12].code_present_mask |= block->page_mask;

        block->phys_2 = -1;
        block->page_mask2 = 0;
        block->next_2 = block->prev_2 = NULL;
        if ((block->pc ^ block->endpc) & ~0xfff)
        {
                block->phys_2 = get_phys_noabrt(block->endpc);
                if (block->phys_2 != -1)
                {
//                pclog("start block - %08x %08x %p %p %p  %08x\n", block->pc, block->endpc, (void *)block, (void *)block->next_2, (void *)pages[block->phys_2 >> 12].block_2, block->phys_2);

                        start_pc = 0;
                        end_pc = (block->endpc & 0xfff) >> PAGE_MASK_SHIFT;
                        for (; start_pc <= end_pc; start_pc++)
                                block->page_mask2 |= ((uint64_t)1 << start_pc);
                
                        if (!pages[block->phys_2 >> 12].block_2)
                                mem_flush_write_page(block->phys_2, block->endpc);
//                pclog("New block - %08x %08x %p %p  phys %08x %08x %016llx\n", block->pc, block->endpc, (void *)block, (void *)block->next_2, block->phys, block->phys_2, block->page_mask2);
                        if (!block->page_mask2)
                                fatal("!page_mask2\n");
                        if (block->next_2)
                        {
//                        pclog("  next_2->pc=%08x\n", block->next_2->pc);
                                if (!block->next_2->pc)
                                        fatal("block->next_2->pc=0 %p\n", (void *)block->next_2);
                        }
                }
        }

//        pclog("block_end: %08x %08x %016llx\n", block->pc, block->endpc, block->page_mask);
        recomp_page = -1;
}

void codegen_block_end()
{
        codeblock_t *block = &codeblock[block_current];

        codegen_block_generate_end_mask();
        add_to_block_list(block);
}

void codegen_block_end_recompile(codeblock_t *block)
{
        codegen_timing_block_end();

        if (codegen_block_cycles)
        {
                addbyte(0x81); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x6d);
                addbyte(cpu_state_offset(_cycles));
                addlong(codegen_block_cycles);
        }
        if (codegen_block_ins)
        {
                addbyte(0x81); /*ADD $codegen_block_ins,ins*/
                addbyte(0x45);
                addbyte(cpu_state_offset(cpu_recomp_ins));
                addlong(codegen_block_ins);
        }
#if 0
        if (codegen_block_full_ins)
        {
                addbyte(0x81); /*ADD $codegen_block_ins,ins*/
                addbyte(0x05);
                addlong((uint32_t)&cpu_recomp_full_ins);
                addlong(codegen_block_full_ins);
        }
#endif
        addbyte(0x83); /*ADDL $16,%esp*/
        addbyte(0xC4);
        addbyte(0x10);
        addbyte(0x5f); /*POP EDI*/
        addbyte(0x5e); /*POP ESI*/
        addbyte(0x5d); /*POP EBP*/
        addbyte(0x5b); /*POP EDX*/
        addbyte(0xC3); /*RET*/
        
        if (block_pos > BLOCK_GPF_OFFSET)
                fatal("Over limit!\n");

        remove_from_block_list(block, block->pc);
        block->next = block->prev = NULL;
        block->next_2 = block->prev_2 = NULL;
        codegen_block_generate_end_mask();
        add_to_block_list(block);
//        pclog("End block %i\n", block_num);
}

void codegen_flush()
{
        return;
}

static int opcode_conditional_jump[256] = 
{
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 1,  /*00*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*10*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*20*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*30*/

        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*40*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*50*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*60*/
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  /*70*/
        
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*80*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*90*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*a0*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*b0*/
        
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*c0*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*d0*/
        1, 1, 1, 1,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*e0*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*f0*/
};

static int opcode_modrm[256] =
{
        1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0,  /*00*/
        1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0,  /*10*/
        1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0,  /*20*/
        1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0,  /*30*/

        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*40*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*50*/
        0, 0, 1, 1,  0, 0, 0, 0,  0, 1, 0, 1,  0, 0, 0, 0,  /*60*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*70*/

        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  /*80*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*90*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*a0*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*b0*/

        1, 1, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0,  0, 0, 0, 0,  /*c0*/
        1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  1, 1, 1, 1,  /*d0*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  /*e0*/
        0, 0, 0, 0,  0, 0, 1, 1,  0, 0, 0, 0,  0, 0, 1, 1,  /*f0*/
};
int opcode_0f_modrm[256] =
{
        1, 1, 1, 1,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*00*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*10*/
        1, 1, 1, 1,  1, 1, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*20*/
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*30*/

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
        
void codegen_debug()
{
        if (output)
        {
                pclog("At %04x(%08x):%04x  %04x(%08x):%04x  es=%08x EAX=%08x BX=%04x ECX=%08x BP=%04x EDX=%08x EDI=%08x\n", CS, cs, cpu_state.pc, SS, ss, ESP,  es,EAX, BX,ECX,BP,  EDX,EDI);
        }
}

static x86seg *codegen_generate_ea_16_long(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc)
{
        if (!cpu_mod && cpu_rm == 6) 
        { 
                addbyte(0xC7); /*MOVL $0,(ssegs)*/
                addbyte(0x45);
                addbyte(cpu_state_offset(eaaddr));
                addlong((fetchdat >> 8) & 0xffff);
                (*op_pc) += 2;
        }
        else
        {
                switch (cpu_mod)
                {
                        case 0:
                        addbyte(0xa1); /*MOVL *mod1add[0][cpu_rm], %eax*/
                        addlong((uint32_t)mod1add[0][cpu_rm]);
                        addbyte(0x03); /*ADDL *mod1add[1][cpu_rm], %eax*/
                        addbyte(0x05);
                        addlong((uint32_t)mod1add[1][cpu_rm]);
                        break;
                        case 1:
                        addbyte(0xb8); /*MOVL ,%eax*/
                        addlong((uint32_t)(int8_t)(rmdat >> 8));// pc++;
                        addbyte(0x03); /*ADDL *mod1add[0][cpu_rm], %eax*/
                        addbyte(0x05);
                        addlong((uint32_t)mod1add[0][cpu_rm]);
                        addbyte(0x03); /*ADDL *mod1add[1][cpu_rm], %eax*/
                        addbyte(0x05);
                        addlong((uint32_t)mod1add[1][cpu_rm]);
                        (*op_pc)++;
                        break;
                        case 2:
                        addbyte(0xb8); /*MOVL ,%eax*/
                        addlong((fetchdat >> 8) & 0xffff);// pc++;
                        addbyte(0x03); /*ADDL *mod1add[0][cpu_rm], %eax*/
                        addbyte(0x05);
                        addlong((uint32_t)mod1add[0][cpu_rm]);
                        addbyte(0x03); /*ADDL *mod1add[1][cpu_rm], %eax*/
                        addbyte(0x05);
                        addlong((uint32_t)mod1add[1][cpu_rm]);
                        (*op_pc) += 2;
                        break;
                }
                addbyte(0x25); /*ANDL $0xffff, %eax*/
                addlong(0xffff);
                addbyte(0xa3);
                addlong((uint32_t)&cpu_state.eaaddr);

                if (mod1seg[cpu_rm] == &ss && !op_ssegs)
                        op_ea_seg = &_ss;
        }
        return op_ea_seg;
}

static x86seg *codegen_generate_ea_32_long(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, int stack_offset)
{
        uint32_t new_eaaddr;

        if (cpu_rm == 4)
        {
                uint8_t sib = fetchdat >> 8;
                (*op_pc)++;
                
                switch (cpu_mod)
                {
                        case 0:
                        if ((sib & 7) == 5)
                        {
                                new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                                addbyte(0xb8); /*MOVL ,%eax*/
                                addlong(new_eaaddr);// pc++;
                                (*op_pc) += 4;
                        }
                        else
                        {
                                addbyte(0x8b); /*MOVL regs[sib&7].l, %eax*/
                                addbyte(0x45);
                                addbyte(cpu_state_offset(regs[sib & 7].l));
                        }
                        break;
                        case 1: 
                        new_eaaddr = (uint32_t)(int8_t)((fetchdat >> 16) & 0xff);
                        addbyte(0xb8); /*MOVL new_eaaddr, %eax*/
                        addlong(new_eaaddr);
                        addbyte(0x03); /*ADDL regs[sib&7].l, %eax*/
                        addbyte(0x45);
                        addbyte(cpu_state_offset(regs[sib & 7].l));
                        (*op_pc)++;
                        break;
                        case 2:
                        new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                        addbyte(0xb8); /*MOVL new_eaaddr, %eax*/
                        addlong(new_eaaddr);
                        addbyte(0x03); /*ADDL regs[sib&7].l, %eax*/
                        addbyte(0x45);
                        addbyte(cpu_state_offset(regs[sib & 7].l));
                        (*op_pc) += 4;
                        break;
                }
                if (stack_offset && (sib & 7) == 4 && (cpu_mod || (sib & 7) != 5)) /*ESP*/
                {
                        addbyte(0x05);
                        addlong(stack_offset);
                }
                if (((sib & 7) == 4 || (cpu_mod && (sib & 7) == 5)) && !op_ssegs)
                        op_ea_seg = &_ss;
                if (((sib >> 3) & 7) != 4)
                {
                        switch (sib >> 6)
                        {
                                case 0:
                                addbyte(0x03); /*ADDL regs[sib&7].l, %eax*/
                                addbyte(0x45);
                                addbyte(cpu_state_offset(regs[(sib >> 3) & 7].l));
                                break;
                                case 1:
                                addbyte(0x8B); addbyte(0x5D); addbyte(cpu_state_offset(regs[(sib >> 3) & 7].l)); /*MOVL armregs[RD],%ebx*/
                                addbyte(0x01); addbyte(0xD8); /*ADDL %ebx,%eax*/
                                addbyte(0x01); addbyte(0xD8); /*ADDL %ebx,%eax*/
                                break;
                                case 2:
                                addbyte(0x8B); addbyte(0x5D); addbyte(cpu_state_offset(regs[(sib >> 3) & 7].l)); /*MOVL armregs[RD],%ebx*/
                                addbyte(0xC1); addbyte(0xE3); addbyte(2); /*SHL $2,%ebx*/
                                addbyte(0x01); addbyte(0xD8); /*ADDL %ebx,%eax*/
                                break;
                                case 3:
                                addbyte(0x8B); addbyte(0x5D); addbyte(cpu_state_offset(regs[(sib >> 3) & 7].l)); /*MOVL armregs[RD],%ebx*/
                                addbyte(0xC1); addbyte(0xE3); addbyte(3); /*SHL $2,%ebx*/
                                addbyte(0x01); addbyte(0xD8); /*ADDL %ebx,%eax*/
                                break;
                        }
                }
                addbyte(0xa3);
                addlong((uint32_t)&cpu_state.eaaddr);
        }
        else
        {
                if (!cpu_mod && cpu_rm == 5)
                {                
                        new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                        addbyte(0xC7); /*MOVL $new_eaaddr,(eaaddr)*/
                        addbyte(0x45);
                        addbyte(cpu_state_offset(eaaddr));
                        addlong(new_eaaddr);
                        (*op_pc) += 4;
                        return op_ea_seg;
                }
                addbyte(0x8b); /*MOVL regs[sib&7].l, %eax*/
                addbyte(0x45);
                addbyte(cpu_state_offset(regs[cpu_rm].l));
//                addbyte(0xa1); /*MOVL regs[cpu_rm].l, %eax*/
//                addlong((uint32_t)&cpu_state.regs[cpu_rm].l);
                cpu_state.eaaddr = cpu_state.regs[cpu_rm].l;
                if (cpu_mod) 
                {
                        if (cpu_rm == 5 && !op_ssegs)
                                op_ea_seg = &_ss;
                        if (cpu_mod == 1) 
                        {
                                addbyte(0x05);
                                addlong((uint32_t)(int8_t)(fetchdat >> 8)); 
                                (*op_pc)++; 
                        }
                        else          
                        {
                                new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                                addbyte(0x05);
                                addlong(new_eaaddr); 
                                (*op_pc) += 4;
                        }
                }
                addbyte(0xa3);
                addlong((uint32_t)&cpu_state.eaaddr);
        }
        return op_ea_seg;
}

void codegen_generate_call(uint8_t opcode, OpFn op, uint32_t fetchdat, uint32_t new_pc, uint32_t old_pc)
{
        codeblock_t *block = &codeblock[block_current];
        uint32_t op_32 = use32;
        uint32_t op_pc = new_pc;
        OpFn *op_table = x86_dynarec_opcodes;
        RecompOpFn *recomp_op_table = recomp_opcodes;
        int opcode_shift = 0;
        int opcode_mask = 0x3ff;
        int over = 0;
        int pc_off = 0;
        int test_modrm = 1;
        int c;
        
        op_ea_seg = &_ds;
        op_ssegs = 0;
        op_old_pc = old_pc;
        
        for (c = 0; c < NR_HOST_REGS; c++)
                host_reg_mapping[c] = -1;
        mmx_ebx_ecx_loaded = 0;
        for (c = 0; c < NR_HOST_XMM_REGS; c++)
                host_reg_xmm_mapping[c] = -1;
        
        codegen_timing_start();

        while (!over)
        {
                switch (opcode)
                {
                        case 0x0f:
                        op_table = x86_dynarec_opcodes_0f;
                        recomp_op_table = recomp_opcodes_0f;
                        over = 1;
                        break;
                        
                        case 0x26: /*ES:*/
                        op_ea_seg = &_es;
                        op_ssegs = 1;
                        break;
                        case 0x2e: /*CS:*/
                        op_ea_seg = &_cs;
                        op_ssegs = 1;
                        break;
                        case 0x36: /*SS:*/
                        op_ea_seg = &_ss;
                        op_ssegs = 1;
                        break;
                        case 0x3e: /*DS:*/
                        op_ea_seg = &_ds;
                        op_ssegs = 1;
                        break;
                        case 0x64: /*FS:*/
                        op_ea_seg = &_fs;
                        op_ssegs = 1;
                        break;
                        case 0x65: /*GS:*/
                        op_ea_seg = &_gs;
                        op_ssegs = 1;
                        break;
                        
                        case 0x66: /*Data size select*/
                        op_32 = ((use32 & 0x100) ^ 0x100) | (op_32 & 0x200);
                        break;
                        case 0x67: /*Address size select*/
                        op_32 = ((use32 & 0x200) ^ 0x200) | (op_32 & 0x100);
                        break;
                        
                        case 0xd8:
                        op_table = (op_32 & 0x200) ? x86_dynarec_opcodes_d8_a32 : x86_dynarec_opcodes_d8_a16;
                        recomp_op_table = recomp_opcodes_d8;
                        opcode_shift = 3;
                        opcode_mask = 0x1f;
                        over = 1;
                        pc_off = -1;
                        test_modrm = 0;
                        break;
                        case 0xd9:
                        op_table = (op_32 & 0x200) ? x86_dynarec_opcodes_d9_a32 : x86_dynarec_opcodes_d9_a16;
                        recomp_op_table = recomp_opcodes_d9;
                        opcode_mask = 0xff;
                        over = 1;
                        pc_off = -1;
                        test_modrm = 0;
                        break;
                        case 0xda:
                        op_table = (op_32 & 0x200) ? x86_dynarec_opcodes_da_a32 : x86_dynarec_opcodes_da_a16;
                        recomp_op_table = recomp_opcodes_da;
                        opcode_mask = 0xff;
                        over = 1;
                        pc_off = -1;
                        test_modrm = 0;
                        break;
                        case 0xdb:
                        op_table = (op_32 & 0x200) ? x86_dynarec_opcodes_db_a32 : x86_dynarec_opcodes_db_a16;
                        recomp_op_table = recomp_opcodes_db;
                        opcode_mask = 0xff;
                        over = 1;
                        pc_off = -1;
                        test_modrm = 0;
                        break;
                        case 0xdc:
                        op_table = (op_32 & 0x200) ? x86_dynarec_opcodes_dc_a32 : x86_dynarec_opcodes_dc_a16;
                        recomp_op_table = recomp_opcodes_dc;
                        opcode_shift = 3;
                        opcode_mask = 0x1f;
                        over = 1;
                        pc_off = -1;
                        test_modrm = 0;
                        break;
                        case 0xdd:
                        op_table = (op_32 & 0x200) ? x86_dynarec_opcodes_dd_a32 : x86_dynarec_opcodes_dd_a16;
                        recomp_op_table = recomp_opcodes_dd;
                        opcode_mask = 0xff;
                        over = 1;
                        pc_off = -1;
                        test_modrm = 0;
                        break;
                        case 0xde:
                        op_table = (op_32 & 0x200) ? x86_dynarec_opcodes_de_a32 : x86_dynarec_opcodes_de_a16;
                        recomp_op_table = recomp_opcodes_de;
                        opcode_mask = 0xff;
                        over = 1;
                        pc_off = -1;
                        test_modrm = 0;
                        break;
                        case 0xdf:
                        op_table = (op_32 & 0x200) ? x86_dynarec_opcodes_df_a32 : x86_dynarec_opcodes_df_a16;
                        recomp_op_table = recomp_opcodes_df;
                        opcode_mask = 0xff;
                        over = 1;
                        pc_off = -1;
                        test_modrm = 0;
                        break;
                        
                        case 0xf0: /*LOCK*/
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
        codegen_timing_opcode(opcode, fetchdat, op_32);
        
        if ((op_table == x86_dynarec_opcodes &&
             ((opcode & 0xf0) == 0x70 || (opcode & 0xfc) == 0xe0 || opcode == 0xc2 ||
              (opcode & 0xfe) == 0xca || (opcode & 0xfc) == 0xcc || (opcode & 0xfc) == 0xe8 ||
              (opcode == 0xff && ((fetchdat & 0x38) >= 0x10 && (fetchdat & 0x38) < 0x30))) ||
            (op_table == x86_dynarec_opcodes_0f && ((opcode & 0xf0) == 0x80))))
        {
                /*Opcode is likely to cause block to exit, update cycle count*/
                if (codegen_block_cycles)
                {
                        addbyte(0x81); /*SUB $codegen_block_cycles, cyclcs*/
                        addbyte(0x6d);
                        addbyte(cpu_state_offset(_cycles));
                        addlong(codegen_block_cycles);
                        codegen_block_cycles = 0;
                }
                if (codegen_block_ins)
                {
                        addbyte(0x81); /*ADD $codegen_block_ins,ins*/
                        addbyte(0x45);
                        addbyte(cpu_state_offset(cpu_recomp_ins));
                        addlong(codegen_block_ins);
                        codegen_block_ins = 0;
                }
#if 0
                if (codegen_block_full_ins)
                {
                        addbyte(0x81); /*ADD $codegen_block_ins,ins*/
                        addbyte(0x05);
                        addlong((uint32_t)&cpu_recomp_full_ins);
                        addlong(codegen_block_full_ins);
                        codegen_block_full_ins = 0;
                }
#endif
        }

        if (recomp_op_table && recomp_op_table[(opcode | op_32) & 0x1ff])
        {
                uint32_t new_pc = recomp_op_table[(opcode | op_32) & 0x1ff](opcode, fetchdat, op_32, op_pc, block);
                if (new_pc)
                {
                        if (new_pc != -1)
                                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.pc, new_pc);

                        codegen_block_ins++;
                        block->ins++;
                        codegen_block_full_ins++;
                        codegen_endpc = (cs + cpu_state.pc) + 8;

                        return;
                }
        }
        
        op = op_table[((opcode >> opcode_shift) | op_32) & opcode_mask];
//        if (output)
//                pclog("Generate call at %08X %02X %08X %02X  %08X %08X %08X %08X %08X  %02X %02X %02X %02X\n", &codeblock[block_current][block_pos], opcode, new_pc, ram[old_pc], EAX, EBX, ECX, EDX, ESI, ram[0x7bd2+6],ram[0x7bd2+7],ram[0x7bd2+8],ram[0x7bd2+9]);
        if (op_ssegs != last_ssegs)
        {
                last_ssegs = op_ssegs;

                addbyte(0xC6); /*MOVB [ssegs],op_ssegs*/
                addbyte(0x45);
                addbyte(cpu_state_offset(ssegs));
                addbyte(op_pc + pc_off);
        }

        if (!test_modrm ||
                (op_table == x86_dynarec_opcodes && opcode_modrm[opcode]) ||
                (op_table == x86_dynarec_opcodes_0f && opcode_0f_modrm[opcode]))
        {
                int stack_offset = 0;
                
                if (op_table == x86_dynarec_opcodes && opcode == 0x8f) /*POP*/
                        stack_offset = (op_32 & 0x100) ? 4 : 2;

                cpu_mod = (fetchdat >> 6) & 3;
                cpu_reg = (fetchdat >> 3) & 7;
                cpu_rm = fetchdat & 7;

                addbyte(0xC7); /*MOVL $rm | mod | reg,(rm_mod_reg_data)*/
                addbyte(0x45);
                addbyte(cpu_state_offset(rm_data.rm_mod_reg_data));
                addlong(cpu_rm | (cpu_mod << 8) | (cpu_reg << 16));

                op_pc += pc_off;
                if (cpu_mod != 3 && !(op_32 & 0x200))
                        op_ea_seg = codegen_generate_ea_16_long(op_ea_seg, fetchdat, op_ssegs, &op_pc);
                if (cpu_mod != 3 &&  (op_32 & 0x200))
                        op_ea_seg = codegen_generate_ea_32_long(op_ea_seg, fetchdat, op_ssegs, &op_pc, stack_offset);
                op_pc -= pc_off;
        }

        if (op_ea_seg != last_ea_seg)
        {
                last_ea_seg = op_ea_seg;
                addbyte(0xC7); /*MOVL $&_ds,(ea_seg)*/
                addbyte(0x45);
                addbyte(cpu_state_offset(ea_seg));
                addlong((uint32_t)op_ea_seg);
        }

        addbyte(0xC7); /*MOVL pc,new_pc*/
        addbyte(0x45);
        addbyte(cpu_state_offset(pc));
        addlong(op_pc + pc_off);

        addbyte(0xC7); /*MOVL $old_pc,(oldpc)*/
        addbyte(0x45);
        addbyte(cpu_state_offset(oldpc));
        addlong(old_pc);

        if (op_32 != last_op32)
        {
                last_op32 = op_32;
                addbyte(0xC7); /*MOVL $use32,(op32)*/
                addbyte(0x45);
                addbyte(cpu_state_offset(op32));
                addlong(op_32);
        }

        addbyte(0xC7); /*MOVL $fetchdat,(%esp)*/
        addbyte(0x04);
        addbyte(0x24);
        addlong(fetchdat);
  
        addbyte(0xE8); /*CALL*/
        addlong(((uint8_t *)op - (uint8_t *)(&block->data[block_pos + 4])));

        codegen_block_ins++;
        
        block->ins++;

        addbyte(0x09); /*OR %eax, %eax*/
        addbyte(0xc0);
        addbyte(0x0F); addbyte(0x85); /*JNZ 0*/
        addlong((uint32_t)&block->data[BLOCK_EXIT_OFFSET] - (uint32_t)(&block->data[block_pos + 4]));

//        addbyte(0xE8); /*CALL*/
//        addlong(((uint8_t *)codegen_debug - (uint8_t *)(&block->data[block_pos + 4])));

        codegen_endpc = (cs + cpu_state.pc) + 8;
}

#endif
