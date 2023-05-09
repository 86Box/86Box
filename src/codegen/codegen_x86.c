/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Dynamic Recompiler for Intel 32-bit systems.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2018 Fred N. van Kempen.
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
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
#if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86

#    include <stdio.h>
#    include <stdint.h>
#    include <string.h>
#    include <stdlib.h>
#    include <wchar.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include "x86.h"
#    include "x86_flags.h"
#    include "x86_ops.h"
#    include "x87.h"
/*ex*/
#    include <86box/nmi.h>
#    include <86box/pic.h>

#    include "386_common.h"

#    include "codegen.h"
#    include "codegen_accumulate.h"
#    include "codegen_ops.h"
#    include "codegen_ops_x86.h"

#    ifdef __unix__
#        include <sys/mman.h>
#        include <unistd.h>
#    endif
#    if defined _WIN32
#        include <windows.h>
#    endif

int      codegen_flat_ds, codegen_flat_ss;
int      mmx_ebx_ecx_loaded;
int      codegen_flags_changed = 0;
int      codegen_fpu_entered   = 0;
int      codegen_mmx_entered   = 0;
int      codegen_fpu_loaded_iq[8];
x86seg  *op_ea_seg;
int      op_ssegs;
uint32_t op_old_pc;

uint32_t recomp_page = -1;

int           host_reg_mapping[NR_HOST_REGS];
int           host_reg_xmm_mapping[NR_HOST_XMM_REGS];
codeblock_t  *codeblock;
codeblock_t **codeblock_hash;

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

static uint32_t mem_abrt_rout;
uint32_t        mem_load_addr_ea_b;
uint32_t        mem_load_addr_ea_w;
uint32_t        mem_load_addr_ea_l;
uint32_t        mem_load_addr_ea_q;
uint32_t        mem_store_addr_ea_b;
uint32_t        mem_store_addr_ea_w;
uint32_t        mem_store_addr_ea_l;
uint32_t        mem_store_addr_ea_q;
uint32_t        mem_load_addr_ea_b_no_abrt;
uint32_t        mem_store_addr_ea_b_no_abrt;
uint32_t        mem_load_addr_ea_w_no_abrt;
uint32_t        mem_store_addr_ea_w_no_abrt;
uint32_t        mem_load_addr_ea_l_no_abrt;
uint32_t        mem_store_addr_ea_l_no_abrt;
uint32_t        mem_check_write;
uint32_t        mem_check_write_w;
uint32_t        mem_check_write_l;

static uint32_t
gen_MEM_LOAD_ADDR_EA_B(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    addbyte(0x89); /*MOV ESI, EDX*/
    addbyte(0xd6);
    addbyte(0x01); /*ADDL EDX, EAX*/
    addbyte(0xc2);
    addbyte(0x89); /*MOV EDI, EDX*/
    addbyte(0xd7);
    addbyte(0xc1); /*SHR EDX, 12*/
    addbyte(0xea);
    addbyte(12);
    addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
    addbyte(0x14);
    addbyte(0x95);
    addlong((uint32_t) readlookup2);
    addbyte(0x83); /*CMP EDX, -1*/
    addbyte(0xfa);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(4 + 1);
    addbyte(0x0f); /*MOVZX EAX, B[EDX+EDI]*/
    addbyte(0xb6);
    addbyte(0x04);
    addbyte(0x3a);
    addbyte(0xc3); /*RET*/

    addbyte(0x01); /*slowpath: ADD ESI,EAX*/
    addbyte(0xc6);
    addbyte(0x56); /*PUSH ESI*/
    addbyte(0xe8); /*CALL readmembl*/
    addlong((uint32_t) readmembl - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 4*/
    addbyte(0xc4);
    addbyte(4);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*MOVZX EAX, AL*/
    addbyte(0xb6);
    addbyte(0xc0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    addbyte(0xc3); /*RET*/

    return addr;
}

static uint32_t
gen_MEM_LOAD_ADDR_EA_W(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    addbyte(0x89); /*MOV ESI, EDX*/
    addbyte(0xd6);
    addbyte(0x01); /*ADDL EDX, EAX*/
    addbyte(0xc2);
    addbyte(0x89); /*MOV EDI, EDX*/
    addbyte(0xd7);
    addbyte(0xc1); /*SHR EDX, 12*/
    addbyte(0xea);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 1*/
    addbyte(0xc7);
    addlong(1);
    addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
    addbyte(0x14);
    addbyte(0x95);
    addlong((uint32_t) readlookup2);
    addbyte(0x75); /*JNE slowpath*/
    addbyte(3 + 2 + 4 + 1);
    addbyte(0x83); /*CMP EDX, -1*/
    addbyte(0xfa);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(4 + 1);
    addbyte(0x0f); /*MOVZX EAX, [EDX+EDI]W*/
    addbyte(0xb7);
    addbyte(0x04);
    addbyte(0x3a);
    addbyte(0xc3); /*RET*/

    addbyte(0x01); /*slowpath: ADD ESI,EAX*/
    addbyte(0xc6);
    addbyte(0x56); /*PUSH ESI*/
    addbyte(0xe8); /*CALL readmemwl*/
    addlong((uint32_t) readmemwl - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 4*/
    addbyte(0xc4);
    addbyte(4);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*MOVZX EAX, AX*/
    addbyte(0xb7);
    addbyte(0xc0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    addbyte(0xc3); /*RET*/

    return addr;
}

static uint32_t
gen_MEM_LOAD_ADDR_EA_L(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    addbyte(0x89); /*MOV ESI, EDX*/
    addbyte(0xd6);
    addbyte(0x01); /*ADDL EDX, EAX*/
    addbyte(0xc2);
    addbyte(0x89); /*MOV EDI, EDX*/
    addbyte(0xd7);
    addbyte(0xc1); /*SHR EDX, 12*/
    addbyte(0xea);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 3*/
    addbyte(0xc7);
    addlong(3);
    addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
    addbyte(0x14);
    addbyte(0x95);
    addlong((uint32_t) readlookup2);
    addbyte(0x75); /*JNE slowpath*/
    addbyte(3 + 2 + 3 + 1);
    addbyte(0x83); /*CMP EDX, -1*/
    addbyte(0xfa);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(3 + 1);
    addbyte(0x8b); /*MOV EAX, [EDX+EDI]*/
    addbyte(0x04);
    addbyte(0x3a);
    addbyte(0xc3); /*RET*/

    addbyte(0x01); /*slowpath: ADD ESI,EAX*/
    addbyte(0xc6);
    addbyte(0x56); /*PUSH ESI*/
    addbyte(0xe8); /*CALL readmemll*/
    addlong((uint32_t) readmemll - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 4*/
    addbyte(0xc4);
    addbyte(4);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    addbyte(0xc3); /*RET*/

    return addr;
}

static uint32_t
gen_MEM_LOAD_ADDR_EA_Q(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    addbyte(0x89); /*MOV ESI, EDX*/
    addbyte(0xd6);
    addbyte(0x01); /*ADDL EDX, EAX*/
    addbyte(0xc2);
    addbyte(0x89); /*MOV EDI, EDX*/
    addbyte(0xd7);
    addbyte(0xc1); /*SHR EDX, 12*/
    addbyte(0xea);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 7*/
    addbyte(0xc7);
    addlong(7);
    addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
    addbyte(0x14);
    addbyte(0x95);
    addlong((uint32_t) readlookup2);
    addbyte(0x75); /*JNE slowpath*/
    addbyte(3 + 2 + 3 + 4 + 1);
    addbyte(0x83); /*CMP EDX, -1*/
    addbyte(0xfa);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(3 + 4 + 1);
    addbyte(0x8b); /*MOV EAX, [EDX+EDI]*/
    addbyte(0x04);
    addbyte(0x3a);
    addbyte(0x8b); /*MOV EDX, [EDX+EDI+4]*/
    addbyte(0x54);
    addbyte(0x3a);
    addbyte(4);
    addbyte(0xc3); /*RET*/

    addbyte(0x01); /*slowpath: ADD ESI,EAX*/
    addbyte(0xc6);
    addbyte(0x56); /*PUSH ESI*/
    addbyte(0xe8); /*CALL readmemql*/
    addlong((uint32_t) readmemql - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 4*/
    addbyte(0xc4);
    addbyte(4);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    addbyte(0xc3); /*RET*/

    return addr;
}

static uint32_t
gen_MEM_STORE_ADDR_EA_B(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*dat = ECX, seg = ESI, addr = EAX*/
    addbyte(0x89); /*MOV EBX, ESI*/
    addbyte(0xf3);
    addbyte(0x01); /*ADDL ESI, EAX*/
    addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
    addbyte(0x89); /*MOV EDI, ESI*/
    addbyte(0xc0 | (REG_ESI << 3) | REG_EDI);
    addbyte(0xc1); /*SHR ESI, 12*/
    addbyte(0xe8 | REG_ESI);
    addbyte(12);
    addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
    addbyte(0x04 | (REG_ESI << 3));
    addbyte(0x85 | (REG_ESI << 3));
    addlong((uint32_t) writelookup2);
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xf8 | REG_ESI);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(3 + 1);
    addbyte(0x88); /*MOV [EDI+ESI],CL*/
    addbyte(0x04 | (REG_ECX << 3));
    addbyte(REG_EDI | (REG_ESI << 3));
    addbyte(0xc3); /*RET*/

    addbyte(0x51); /*slowpath: PUSH ECX*/
    addbyte(0x01); /*ADD EBX,EAX*/
    addbyte(0xC3);
    addbyte(0x53); /*PUSH EBX*/
    addbyte(0xe8); /*CALL writemembl*/
    addlong((uint32_t) writemembl - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 8*/
    addbyte(0xc4);
    addbyte(8);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    addbyte(0xc3); /*RET*/

    return addr;
}

static uint32_t
gen_MEM_STORE_ADDR_EA_W(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*dat = ECX, seg = ESI, addr = EAX*/
    addbyte(0x89); /*MOV EBX, ESI*/
    addbyte(0xf3);
    addbyte(0x01); /*ADDL ESI, EAX*/
    addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
    addbyte(0x89); /*MOV EDI, ESI*/
    addbyte(0xf7);
    addbyte(0xc1); /*SHR ESI, 12*/
    addbyte(0xe8 | REG_ESI);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 1*/
    addbyte(0xc7);
    addlong(1);
    addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
    addbyte(0x04 | (REG_ESI << 3));
    addbyte(0x85 | (REG_ESI << 3));
    addlong((uint32_t) writelookup2);
    addbyte(0x75); /*JNE slowpath*/
    addbyte(3 + 2 + 4 + 1);
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xf8 | REG_ESI);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(4 + 1);
    addbyte(0x66); /*MOV [EDI+ESI],CX*/
    addbyte(0x89);
    addbyte(0x04 | (REG_CX << 3));
    addbyte(REG_EDI | (REG_ESI << 3));
    addbyte(0xc3); /*RET*/

    addbyte(0x51); /*slowpath: PUSH ECX*/
    addbyte(0x01); /*ADD EBX,EAX*/
    addbyte(0xC3);
    addbyte(0x53); /*PUSH EBX*/
    addbyte(0xe8); /*CALL writememwl*/
    addlong((uint32_t) writememwl - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 8*/
    addbyte(0xc4);
    addbyte(8);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    addbyte(0xc3); /*RET*/

    return addr;
}

static uint32_t
gen_MEM_STORE_ADDR_EA_L(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*dat = ECX, seg = ESI, addr = EAX*/
    addbyte(0x89); /*MOV EBX, ESI*/
    addbyte(0xf3);
    addbyte(0x01); /*ADDL ESI, EAX*/
    addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
    addbyte(0x89); /*MOV EDI, ESI*/
    addbyte(0xf7);
    addbyte(0xc1); /*SHR ESI, 12*/
    addbyte(0xe8 | REG_ESI);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 3*/
    addbyte(0xc7);
    addlong(3);
    addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
    addbyte(0x04 | (REG_ESI << 3));
    addbyte(0x85 | (REG_ESI << 3));
    addlong((uint32_t) writelookup2);
    addbyte(0x75); /*JNE slowpath*/
    addbyte(3 + 2 + 3 + 1);
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xf8 | REG_ESI);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(3 + 1);
    addbyte(0x89); /*MOV [EDI+ESI],ECX*/
    addbyte(0x04 | (REG_ECX << 3));
    addbyte(REG_EDI | (REG_ESI << 3));
    addbyte(0xc3); /*RET*/

    addbyte(0x51); /*slowpath: PUSH ECX*/
    addbyte(0x01); /*ADD EBX,EAX*/
    addbyte(0xC3);
    addbyte(0x53); /*PUSH EBX*/
    addbyte(0xe8); /*CALL writememll*/
    addlong((uint32_t) writememll - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 8*/
    addbyte(0xc4);
    addbyte(8);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    addbyte(0xc3); /*RET*/

    return addr;
}

static uint32_t
gen_MEM_STORE_ADDR_EA_Q(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*dat = EBX/ECX, seg = ESI, addr = EAX*/
    addbyte(0x89); /*MOV EDX, ESI*/
    addbyte(0xf2);
    addbyte(0x01); /*ADDL ESI, EAX*/
    addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
    addbyte(0x89); /*MOV EDI, ESI*/
    addbyte(0xf7);
    addbyte(0xc1); /*SHR ESI, 12*/
    addbyte(0xe8 | REG_ESI);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 7*/
    addbyte(0xc7);
    addlong(7);
    addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
    addbyte(0x04 | (REG_ESI << 3));
    addbyte(0x85 | (REG_ESI << 3));
    addlong((uint32_t) writelookup2);
    addbyte(0x75); /*JNE slowpath*/
    addbyte(3 + 2 + 3 + 4 + 1);
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xf8 | REG_ESI);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(3 + 4 + 1);
    addbyte(0x89); /*MOV [EDI+ESI],EBX*/
    addbyte(0x04 | (REG_EBX << 3));
    addbyte(REG_EDI | (REG_ESI << 3));
    addbyte(0x89); /*MOV 4[EDI+ESI],EBX*/
    addbyte(0x44 | (REG_ECX << 3));
    addbyte(REG_EDI | (REG_ESI << 3));
    addbyte(4);
    addbyte(0xc3); /*RET*/

    addbyte(0x51); /*slowpath: PUSH ECX*/
    addbyte(0x53); /*PUSH EBX*/
    addbyte(0x01); /*ADD EDX,EAX*/
    addbyte(0xC2);
    addbyte(0x52); /*PUSH EDX*/
    addbyte(0xe8); /*CALL writememql*/
    addlong((uint32_t) writememql - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 12*/
    addbyte(0xc4);
    addbyte(12);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    addbyte(0xc3); /*RET*/

    return addr;
}

#    ifndef RELEASE_BUILD
static char gen_MEM_LOAD_ADDR_EA_B_NO_ABRT_err[] = "gen_MEM_LOAD_ADDR_EA_B_NO_ABRT aborted\n";
#    endif
static uint32_t
gen_MEM_LOAD_ADDR_EA_B_NO_ABRT(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    addbyte(0x89); /*MOV ESI, EDX*/
    addbyte(0xd6);
    addbyte(0x01); /*ADDL EDX, EAX*/
    addbyte(0xc2);
    addbyte(0x89); /*MOV EDI, EDX*/
    addbyte(0xd7);
    addbyte(0xc1); /*SHR EDX, 12*/
    addbyte(0xea);
    addbyte(12);
    addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
    addbyte(0x14);
    addbyte(0x95);
    addlong((uint32_t) readlookup2);
    addbyte(0x83); /*CMP EDX, -1*/
    addbyte(0xfa);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(4 + 1);
    addbyte(0x0f); /*MOVZX ECX, B[EDX+EDI]*/
    addbyte(0xb6);
    addbyte(0x0c);
    addbyte(0x3a);
    addbyte(0xc3); /*RET*/

    addbyte(0x01); /*slowpath: ADD ESI,EAX*/
    addbyte(0xc6);
    addbyte(0x56); /*PUSH ESI*/
    addbyte(0xe8); /*CALL readmembl*/
    addlong((uint32_t) readmembl - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 4*/
    addbyte(0xc4);
    addbyte(4);
#    ifndef RELEASE_BUILD
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
#    endif
    addbyte(0x0f); /*MOVZX ECX, AL*/
    addbyte(0xb6);
    addbyte(0xc8);
#    ifndef RELEASE_BUILD
    addbyte(0x75); /*JNE mem_abrt_rout*/
    addbyte(1);
#    endif
    addbyte(0xc3); /*RET*/
#    ifndef RELEASE_BUILD
    addbyte(0xc7); /*MOV [ESP], gen_MEM_LOAD_ADDR_EA_B_NO_ABRT_err*/
    addbyte(0x04);
    addbyte(0x24);
    addlong((uint32_t) gen_MEM_LOAD_ADDR_EA_B_NO_ABRT_err);
    addbyte(0xe8); /*CALL fatal*/
    addlong((uint32_t) fatal - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    /*Should not return!*/
#    endif
    return addr;
}

#    ifndef RELEASE_BUILD
static char gen_MEM_LOAD_ADDR_EA_W_NO_ABRT_err[] = "gen_MEM_LOAD_ADDR_EA_W_NO_ABRT aborted\n";
#    endif
static uint32_t
gen_MEM_LOAD_ADDR_EA_W_NO_ABRT(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    addbyte(0x89); /*MOV ESI, EDX*/
    addbyte(0xd6);
    addbyte(0x01); /*ADDL EDX, EAX*/
    addbyte(0xc2);
    addbyte(0x89); /*MOV EDI, EDX*/
    addbyte(0xd7);
    addbyte(0xc1); /*SHR EDX, 12*/
    addbyte(0xea);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 1*/
    addbyte(0xc7);
    addlong(1);
    addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
    addbyte(0x14);
    addbyte(0x95);
    addlong((uint32_t) readlookup2);
    addbyte(0x75); /*JNE slowpath*/
    addbyte(3 + 2 + 4 + 1);
    addbyte(0x83); /*CMP EDX, -1*/
    addbyte(0xfa);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(4 + 1);
    addbyte(0x0f); /*MOVZX ECX, [EDX+EDI]W*/
    addbyte(0xb7);
    addbyte(0x0c);
    addbyte(0x3a);
    addbyte(0xc3); /*RET*/

    addbyte(0x01); /*slowpath: ADD ESI,EAX*/
    addbyte(0xc6);
    addbyte(0x56); /*PUSH ESI*/
    addbyte(0xe8); /*CALL readmemwl*/
    addlong((uint32_t) readmemwl - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 4*/
    addbyte(0xc4);
    addbyte(4);
#    ifndef RELEASE_BUILD
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
#    endif
    addbyte(0x0f); /*MOVZX ECX, AX*/
    addbyte(0xb7);
    addbyte(0xc8);
#    ifndef RELEASE_BUILD
    addbyte(0x75); /*JNE mem_abrt_rout*/
    addbyte(1);
#    endif
    addbyte(0xc3); /*RET*/
#    ifndef RELEASE_BUILD
    addbyte(0xc7); /*MOV [ESP], gen_MEM_LOAD_ADDR_EA_W_NO_ABRT_err*/
    addbyte(0x04);
    addbyte(0x24);
    addlong((uint32_t) gen_MEM_LOAD_ADDR_EA_W_NO_ABRT_err);
    addbyte(0xe8); /*CALL fatal*/
    addlong((uint32_t) fatal - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    /*Should not return!*/
#    endif
    return addr;
}

#    ifndef RELEASE_BUILD
static char gen_MEM_LOAD_ADDR_EA_L_NO_ABRT_err[] = "gen_MEM_LOAD_ADDR_EA_L_NO_ABRT aborted\n";
#    endif
static uint32_t
gen_MEM_LOAD_ADDR_EA_L_NO_ABRT(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    addbyte(0x89); /*MOV ESI, EDX*/
    addbyte(0xd6);
    addbyte(0x01); /*ADDL EDX, EAX*/
    addbyte(0xc2);
    addbyte(0x89); /*MOV EDI, EDX*/
    addbyte(0xd7);
    addbyte(0xc1); /*SHR EDX, 12*/
    addbyte(0xea);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 3*/
    addbyte(0xc7);
    addlong(3);
    addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
    addbyte(0x14);
    addbyte(0x95);
    addlong((uint32_t) readlookup2);
    addbyte(0x75); /*JE slowpath*/
    addbyte(3 + 2 + 3 + 1);
    addbyte(0x83); /*CMP EDX, -1*/
    addbyte(0xfa);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(3 + 1);
    addbyte(0x8b); /*MOV ECX, [EDX+EDI]*/
    addbyte(0x0c);
    addbyte(0x3a);
    addbyte(0xc3); /*RET*/

    addbyte(0x01); /*slowpath: ADD ESI,EAX*/
    addbyte(0xc6);
    addbyte(0x56); /*PUSH ESI*/
    addbyte(0xe8); /*CALL readmemll*/
    addlong((uint32_t) readmemll - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 4*/
    addbyte(0xc4);
    addbyte(4);
    addbyte(0x89); /*MOV ECX, EAX*/
    addbyte(0xc1);
#    ifndef RELEASE_BUILD
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x75); /*JNE mem_abrt_rout*/
    addbyte(1);
#    endif
    addbyte(0xc3); /*RET*/
#    ifndef RELEASE_BUILD
    addbyte(0x83); /*SUBL 4,%esp*/
    addbyte(0xEC);
    addbyte(4);
    addbyte(0xc7); /*MOV [ESP], gen_MEM_LOAD_ADDR_EA_L_NO_ABRT_err*/
    addbyte(0x04);
    addbyte(0x24);
    addlong((uint32_t) gen_MEM_LOAD_ADDR_EA_L_NO_ABRT_err);
    addbyte(0xe8); /*CALL fatal*/
    addlong((uint32_t) fatal - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    /*Should not return!*/
#    endif
    return addr;
}

#    ifndef RELEASE_BUILD
static char gen_MEM_STORE_ADDR_EA_B_NO_ABRT_err[] = "gen_MEM_STORE_ADDR_EA_B_NO_ABRT aborted\n";
#    endif
static uint32_t
gen_MEM_STORE_ADDR_EA_B_NO_ABRT(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*dat = ECX, seg = ESI, addr = EAX*/
    addbyte(0x89); /*MOV EBX, ESI*/
    addbyte(0xf3);
    addbyte(0x01); /*ADDL ESI, EAX*/
    addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
    addbyte(0x89); /*MOV EDI, ESI*/
    addbyte(0xc0 | (REG_ESI << 3) | REG_EDI);
    addbyte(0xc1); /*SHR ESI, 12*/
    addbyte(0xe8 | REG_ESI);
    addbyte(12);
    addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
    addbyte(0x04 | (REG_ESI << 3));
    addbyte(0x85 | (REG_ESI << 3));
    addlong((uint32_t) writelookup2);
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xf8 | REG_ESI);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(3 + 1);
    addbyte(0x88); /*MOV [EDI+ESI],CL*/
    addbyte(0x04 | (REG_ECX << 3));
    addbyte(REG_EDI | (REG_ESI << 3));
    addbyte(0xc3); /*RET*/

    addbyte(0x51); /*slowpath: PUSH ECX*/
    addbyte(0x01); /*ADD EBX,EAX*/
    addbyte(0xc3);
    addbyte(0x53); /*PUSH EBX*/
    addbyte(0xe8); /*CALL writemembl*/
    addlong((uint32_t) writemembl - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 8*/
    addbyte(0xc4);
    addbyte(8);
#    ifndef RELEASE_BUILD
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x75); /*JNE mem_abrt_rout*/
    addbyte(1);
#    endif
    addbyte(0xc3); /*RET*/
#    ifndef RELEASE_BUILD
    addbyte(0xc7); /*MOV [ESP], gen_MEM_STORE_ADDR_EA_B_NO_ABRT_err*/
    addbyte(0x04);
    addbyte(0x24);
    addlong((uint32_t) gen_MEM_STORE_ADDR_EA_B_NO_ABRT_err);
    addbyte(0xe8); /*CALL fatal*/
    addlong((uint32_t) fatal - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    /*Should not return!*/
#    endif
    return addr;
}

#    ifndef RELEASE_BUILD
static char gen_MEM_STORE_ADDR_EA_W_NO_ABRT_err[] = "gen_MEM_STORE_ADDR_EA_W_NO_ABRT aborted\n";
#    endif
static uint32_t
gen_MEM_STORE_ADDR_EA_W_NO_ABRT(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*dat = ECX, seg = ESI, addr = EAX*/
    addbyte(0x89); /*MOV EBX, ESI*/
    addbyte(0xf3);
    addbyte(0x01); /*ADDL ESI, EAX*/
    addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
    addbyte(0x89); /*MOV EDI, ESI*/
    addbyte(0xf7);
    addbyte(0xc1); /*SHR ESI, 12*/
    addbyte(0xe8 | REG_ESI);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 1*/
    addbyte(0xc7);
    addlong(1);
    addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
    addbyte(0x04 | (REG_ESI << 3));
    addbyte(0x85 | (REG_ESI << 3));
    addlong((uint32_t) writelookup2);
    addbyte(0x75); /*JNE slowpath*/
    addbyte(3 + 2 + 4 + 1);
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xf8 | REG_ESI);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(4 + 1);
    addbyte(0x66); /*MOV [EDI+ESI],CX*/
    addbyte(0x89);
    addbyte(0x04 | (REG_CX << 3));
    addbyte(REG_EDI | (REG_ESI << 3));
    addbyte(0xc3); /*RET*/

    addbyte(0x51); /*slowpath: PUSH ECX*/
    addbyte(0x01); /*ADD EBX,EAX*/
    addbyte(0xC3);
    addbyte(0x53); /*PUSH EBX*/
    addbyte(0xe8); /*CALL writememwl*/
    addlong((uint32_t) writememwl - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 8*/
    addbyte(0xc4);
    addbyte(8);
#    ifndef RELEASE_BUILD
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x75); /*JNE mem_abrt_rout*/
    addbyte(1);
#    endif
    addbyte(0xc3); /*RET*/
#    ifndef RELEASE_BUILD
    addbyte(0xc7); /*MOV [ESP], gen_MEM_STORE_ADDR_EA_W_NO_ABRT_err*/
    addbyte(0x04);
    addbyte(0x24);
    addlong((uint32_t) gen_MEM_STORE_ADDR_EA_W_NO_ABRT_err);
    addbyte(0xe8); /*CALL fatal*/
    addlong((uint32_t) fatal - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    /*Should not return!*/
#    endif
    return addr;
}

#    ifndef RELEASE_BUILD
static char gen_MEM_STORE_ADDR_EA_L_NO_ABRT_err[] = "gen_MEM_STORE_ADDR_EA_L_NO_ABRT aborted\n";
#    endif
static uint32_t
gen_MEM_STORE_ADDR_EA_L_NO_ABRT(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*dat = ECX, seg = ESI, addr = EAX*/
    addbyte(0x89); /*MOV EBX, ESI*/
    addbyte(0xf3);
    addbyte(0x01); /*ADDL ESI, EAX*/
    addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
    addbyte(0x89); /*MOV EDI, ESI*/
    addbyte(0xf7);
    addbyte(0xc1); /*SHR ESI, 12*/
    addbyte(0xe8 | REG_ESI);
    addbyte(12);
    addbyte(0xf7); /*TEST EDI, 3*/
    addbyte(0xc7);
    addlong(3);
    addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
    addbyte(0x04 | (REG_ESI << 3));
    addbyte(0x85 | (REG_ESI << 3));
    addlong((uint32_t) writelookup2);
    addbyte(0x75); /*JNE slowpath*/
    addbyte(3 + 2 + 3 + 1);
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xf8 | REG_ESI);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(3 + 1);
    addbyte(0x89); /*MOV [EDI+ESI],ECX*/
    addbyte(0x04 | (REG_ECX << 3));
    addbyte(REG_EDI | (REG_ESI << 3));
    addbyte(0xc3); /*RET*/

    addbyte(0x51); /*slowpath: PUSH ECX*/
    addbyte(0x01); /*ADD EBX,EAX*/
    addbyte(0xC3);
    addbyte(0x53); /*PUSH EBX*/
    addbyte(0xe8); /*CALL writememll*/
    addlong((uint32_t) writememll - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 8*/
    addbyte(0xc4);
    addbyte(8);
#    ifndef RELEASE_BUILD
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x75); /*JNE mem_abrt_rout*/
    addbyte(1);
#    endif
    addbyte(0xc3); /*RET*/
#    ifndef RELEASE_BUILD
    addbyte(0xc7); /*MOV [ESP], gen_MEM_STORE_ADDR_EA_L_NO_ABRT_err*/
    addbyte(0x04);
    addbyte(0x24);
    addlong((uint32_t) gen_MEM_STORE_ADDR_EA_L_NO_ABRT_err);
    addbyte(0xe8); /*CALL fatal*/
    addlong((uint32_t) fatal - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    /*Should not return!*/
#    endif
    return addr;
}

static uint32_t
gen_MEM_CHECK_WRITE(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*seg = ESI, addr = EAX*/

    addbyte(0x8d); /*LEA EDI, [EAX+ESI]*/
    addbyte(0x3c);
    addbyte(0x30);
    addbyte(0x83); /*CMP cr0, 0*/
    addbyte(0x3d);
    addlong((uint32_t) &cr0);
    addbyte(0);
    addbyte(0x78); /*JS +*/
    addbyte(1);
    addbyte(0xc3); /*RET*/
    addbyte(0xc1); /*SHR EDI, 12*/
    addbyte(0xef);
    addbyte(12);
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xfe);
    addbyte(-1);
    addbyte(0x74); /*JE slowpath*/
    addbyte(11);
    addbyte(0x83); /*CMP writelookup2[EDI*4],-1*/
    addbyte(0x3c);
    addbyte(0xbd);
    addlong((uint32_t) writelookup2);
    addbyte(-1);
    addbyte(0x74); /*JE +*/
    addbyte(1);
    addbyte(0xc3); /*RET*/

    /*slowpath:*/
    addbyte(0x8d); /*LEA EDI, [EAX+ESI]*/
    addbyte(0x3c);
    addbyte(0x30);
    addbyte(0x6a); /*PUSH 1*/
    addbyte(1);
    addbyte(0x57); /*PUSH EDI*/
    addbyte(0xe8); /*CALL mmutranslatereal32*/
    addlong((uint32_t) mmutranslatereal32 - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x83); /*ADD ESP, 8*/
    addbyte(0xc4);
    addbyte(8);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    addbyte(0xc3); /*RET*/

    return addr;
}

static uint32_t
gen_MEM_CHECK_WRITE_W(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*seg = ESI, addr = EAX*/

    addbyte(0x8d); /*LEA EDI, [EAX+ESI]*/
    addbyte(0x3c);
    addbyte(0x30);
    addbyte(0x83); /*CMP cr0, 0*/
    addbyte(0x3d);
    addlong((uint32_t) &cr0);
    addbyte(0);
    addbyte(0x78); /*JS +*/
    addbyte(1);
    addbyte(0xc3); /*RET*/
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xfe);
    addbyte(-1);
    addbyte(0x8d); /*LEA ESI, 1[EDI]*/
    addbyte(0x77);
    addbyte(0x01);
    addbyte(0x74); /*JE slowpath*/
    addbyte(11);
    addbyte(0x89); /*MOV EAX, EDI*/
    addbyte(0xf8);
    addbyte(0xc1); /*SHR EDI, 12*/
    addbyte(0xef);
    addbyte(12);
    addbyte(0xc1); /*SHR ESI, 12*/
    addbyte(0xee);
    addbyte(12);
    addbyte(0x83); /*CMP writelookup2[EDI*4],-1*/
    addbyte(0x3c);
    addbyte(0xbd);
    addlong((uint32_t) writelookup2);
    addbyte(-1);
    addbyte(0x74); /*JE +*/
    addbyte(11);
    addbyte(0x83); /*CMP writelookup2[ESI*4],-1*/
    addbyte(0x3c);
    addbyte(0xb5);
    addlong((uint32_t) writelookup2);
    addbyte(-1);
    addbyte(0x74); /*JE +*/
    addbyte(1);
    addbyte(0xc3); /*RET*/

    /*slowpath:*/
    addbyte(0x89); /*MOV EDI, EAX*/
    addbyte(0xc7);
    /*slowpath_lp:*/
    addbyte(0x6a); /*PUSH 1*/
    addbyte(1);
    addbyte(0x57); /*PUSH EDI*/
    addbyte(0xe8); /*CALL mmutranslatereal32*/
    addlong((uint32_t) mmutranslatereal32 - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x5f); /*POP EDI*/
    addbyte(0x83); /*ADD ESP, 4*/
    addbyte(0xc4);
    addbyte(4);
    addbyte(0x83); /*ADD EDI, 1*/
    addbyte(0xc7);
    addbyte(1);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    /*If bits 0-11 of the address are now 0 then this crosses a page, so loop back*/
    addbyte(0xf7); /*TEST $fff, EDI*/
    addbyte(0xc7);
    addlong(0xfff);
    addbyte(0x74); /*JE slowpath_lp*/
    addbyte(-33);
    addbyte(0xc3); /*RET*/

    return addr;
}

static uint32_t
gen_MEM_CHECK_WRITE_L(void)
{
    uint32_t addr = (uint32_t) &codeblock[block_current].data[block_pos];

    /*seg = ESI, addr = EAX*/

    addbyte(0x8d); /*LEA EDI, [EAX+ESI]*/
    addbyte(0x3c);
    addbyte(0x30);
    addbyte(0x83); /*CMP cr0, 0*/
    addbyte(0x3d);
    addlong((uint32_t) &cr0);
    addbyte(0);
    addbyte(0x78); /*JS +*/
    addbyte(1);
    addbyte(0xc3); /*RET*/
    addbyte(0x83); /*CMP ESI, -1*/
    addbyte(0xfe);
    addbyte(-1);
    addbyte(0x8d); /*LEA ESI, 3[EDI]*/
    addbyte(0x77);
    addbyte(0x03);
    addbyte(0x74); /*JE slowpath*/
    addbyte(11);
    addbyte(0x89); /*MOV EAX, EDI*/
    addbyte(0xf8);
    addbyte(0xc1); /*SHR EDI, 12*/
    addbyte(0xef);
    addbyte(12);
    addbyte(0xc1); /*SHR ESI, 12*/
    addbyte(0xee);
    addbyte(12);
    addbyte(0x83); /*CMP writelookup2[EDI*4],-1*/
    addbyte(0x3c);
    addbyte(0xbd);
    addlong((uint32_t) writelookup2);
    addbyte(-1);
    addbyte(0x74); /*JE +*/
    addbyte(11);
    addbyte(0x83); /*CMP writelookup2[ESI*4],-1*/
    addbyte(0x3c);
    addbyte(0xb5);
    addlong((uint32_t) writelookup2);
    addbyte(-1);
    addbyte(0x74); /*JE +*/
    addbyte(1);
    addbyte(0xc3); /*RET*/

    /*slowpath:*/
    addbyte(0x89); /*MOV EDI, EAX*/
    addbyte(0xc7);
    /*slowpath_lp:*/
    addbyte(0x6a); /*PUSH 1*/
    addbyte(1);
    addbyte(0x57); /*PUSH EDI*/
    addbyte(0xe8); /*CALL mmutranslatereal32*/
    addlong((uint32_t) mmutranslatereal32 - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
    addbyte(0x5f); /*POP EDI*/
    addbyte(0x83); /*ADD ESP, 4*/
    addbyte(0xc4);
    addbyte(4);
    addbyte(0x83); /*ADD EDI, 3*/
    addbyte(0xc7);
    addbyte(3);
    addbyte(0x80); /*CMP abrt, 0*/
    addbyte(0x7d);
    addbyte((uint8_t) cpu_state_offset(abrt));
    addbyte(0);
    addbyte(0x0f); /*JNE mem_abrt_rout*/
    addbyte(0x85);
    addlong(mem_abrt_rout - ((uint32_t) (&codeblock[block_current].data[block_pos]) + 4));
    /*If bits 2-11 of the address are now 0 then this crosses a page, so loop back*/
    addbyte(0xf7); /*TEST EDI, FFC*/
    addbyte(0xc7);
    addlong(0xffc);
    addbyte(0x74); /*JE slowpath_lp*/
    addbyte(-33);
    addbyte(0xc3); /*RET*/

    return addr;
}

void
codegen_init(void)
{
#    ifdef _WIN32
    codeblock = VirtualAlloc(NULL, (BLOCK_SIZE + 1) * sizeof(codeblock_t), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#    elif defined __unix__
    codeblock = mmap(NULL, (BLOCK_SIZE + 1) * sizeof(codeblock_t), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
#    else
    codeblock = malloc((BLOCK_SIZE + 1) * sizeof(codeblock_t));
#    endif
    codeblock_hash = malloc(HASH_SIZE * sizeof(codeblock_t *));

    memset(codeblock, 0, (BLOCK_SIZE + 1) * sizeof(codeblock_t));
    memset(codeblock_hash, 0, HASH_SIZE * sizeof(codeblock_t *));

    block_current = BLOCK_SIZE;
    block_pos     = 0;
    mem_abrt_rout = (uint32_t) &codeblock[block_current].data[block_pos];
    addbyte(0x83); /*ADDL $16+4,%esp*/
    addbyte(0xC4);
    addbyte(0x10 + 4);
    addbyte(0x5f); /*POP EDI*/
    addbyte(0x5e); /*POP ESI*/
    addbyte(0x5d); /*POP EBP*/
    addbyte(0x5b); /*POP EDX*/
    addbyte(0xC3); /*RET*/
    block_pos                   = (block_pos + 15) & ~15;
    mem_load_addr_ea_l          = (uint32_t) gen_MEM_LOAD_ADDR_EA_L();
    block_pos                   = (block_pos + 15) & ~15;
    mem_load_addr_ea_w          = (uint32_t) gen_MEM_LOAD_ADDR_EA_W();
    block_pos                   = (block_pos + 15) & ~15;
    mem_load_addr_ea_b          = (uint32_t) gen_MEM_LOAD_ADDR_EA_B();
    block_pos                   = (block_pos + 15) & ~15;
    mem_load_addr_ea_q          = (uint32_t) gen_MEM_LOAD_ADDR_EA_Q();
    block_pos                   = (block_pos + 15) & ~15;
    mem_store_addr_ea_l         = (uint32_t) gen_MEM_STORE_ADDR_EA_L();
    block_pos                   = (block_pos + 15) & ~15;
    mem_store_addr_ea_w         = (uint32_t) gen_MEM_STORE_ADDR_EA_W();
    block_pos                   = (block_pos + 15) & ~15;
    mem_store_addr_ea_b         = (uint32_t) gen_MEM_STORE_ADDR_EA_B();
    block_pos                   = (block_pos + 15) & ~15;
    mem_store_addr_ea_q         = (uint32_t) gen_MEM_STORE_ADDR_EA_Q();
    block_pos                   = (block_pos + 15) & ~15;
    mem_load_addr_ea_b_no_abrt  = (uint32_t) gen_MEM_LOAD_ADDR_EA_B_NO_ABRT();
    block_pos                   = (block_pos + 15) & ~15;
    mem_store_addr_ea_b_no_abrt = (uint32_t) gen_MEM_STORE_ADDR_EA_B_NO_ABRT();
    block_pos                   = (block_pos + 15) & ~15;
    mem_load_addr_ea_w_no_abrt  = (uint32_t) gen_MEM_LOAD_ADDR_EA_W_NO_ABRT();
    block_pos                   = (block_pos + 15) & ~15;
    mem_store_addr_ea_w_no_abrt = (uint32_t) gen_MEM_STORE_ADDR_EA_W_NO_ABRT();
    block_pos                   = (block_pos + 15) & ~15;
    mem_load_addr_ea_l_no_abrt  = (uint32_t) gen_MEM_LOAD_ADDR_EA_L_NO_ABRT();
    block_pos                   = (block_pos + 15) & ~15;
    mem_store_addr_ea_l_no_abrt = (uint32_t) gen_MEM_STORE_ADDR_EA_L_NO_ABRT();
    block_pos                   = (block_pos + 15) & ~15;
    mem_check_write             = (uint32_t) gen_MEM_CHECK_WRITE();
    block_pos                   = (block_pos + 15) & ~15;
    mem_check_write_w           = (uint32_t) gen_MEM_CHECK_WRITE_W();
    block_pos                   = (block_pos + 15) & ~15;
    mem_check_write_l           = (uint32_t) gen_MEM_CHECK_WRITE_L();

#    ifndef _MSC_VER
    asm(
        "fstcw %0\n"
        : "=m"(cpu_state.old_npxc));
#    else
    __asm
    {
                fstcw cpu_state.old_npxc
    }
#    endif
}

void
codegen_reset(void)
{
    memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
    memset(codeblock_hash, 0, HASH_SIZE * sizeof(codeblock_t *));
    mem_reset_page_blocks();
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
        if (!block->next->valid)
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

    if (!block->valid)
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
    block->flags                  = CODEBLOCK_STATIC_TOP;
    block->status                 = cpu_cur_status;

    block->was_recompiled = 0;

    recomp_page = block->phys & ~0xfff;

    codeblock_tree_add(block);
}

void
codegen_block_start_recompile(codeblock_t *block)
{
    page_t *page = &pages[block->phys >> 12];

    if (!page->block[(block->phys >> 10) & 3])
        mem_flush_write_page(block->phys, cs + cpu_state.pc);

    block_num     = HASH(block->phys);
    block_current = block->pnt;

    if (block->pc != cs + cpu_state.pc || block->was_recompiled)
        fatal("Recompile to used block!\n");

    block->status = cpu_cur_status;

    block_pos = BLOCK_GPF_OFFSET;
#    ifdef OLD_GPF
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
    addlong((uint32_t) x86gpf - (uint32_t) (&codeblock[block_current].data[block_pos + 4]));
#    else
    addbyte(0xc6); /* mov byte ptr[&(cpu_state.abrt)],ABRT_GPF */
    addbyte(0x05);
    addlong((uint32_t) (uintptr_t) & (cpu_state.abrt));
    addbyte(ABRT_GPF);
    addbyte(0x31); /* xor eax,eax */
    addbyte(0xc0);
    addbyte(0xa3); /* mov [&(abrt_error)],eax */
    addlong((uint32_t) (uintptr_t) & (abrt_error));
#    endif
    block_pos = BLOCK_EXIT_OFFSET; /*Exit code*/
    addbyte(0x83);                 /*ADDL $16,%esp*/
    addbyte(0xC4);
    addbyte(0x10);
    addbyte(0x5f); /*POP EDI*/
    addbyte(0x5e); /*POP ESI*/
    addbyte(0x5d); /*POP EBP*/
    addbyte(0x5b); /*POP EDX*/
    addbyte(0xC3); /*RET*/
    cpu_block_end = 0;
    block_pos     = 0; /*Entry code*/
    addbyte(0x53);     /*PUSH EBX*/
    addbyte(0x55);     /*PUSH EBP*/
    addbyte(0x56);     /*PUSH ESI*/
    addbyte(0x57);     /*PUSH EDI*/
    addbyte(0x83);     /*SUBL $16,%esp*/
    addbyte(0xEC);
    addbyte(0x10);
    addbyte(0xBD); /*MOVL EBP, &cpu_state*/
    addlong(((uintptr_t) &cpu_state) + 128);

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

    block->TOP            = cpu_state.TOP & 7;
    block->was_recompiled = 1;

    codegen_flat_ds = !(cpu_cur_status & CPU_STATUS_NOTFLATDS);
    codegen_flat_ss = !(cpu_cur_status & CPU_STATUS_NOTFLATSS);

    codegen_accumulate_reset();
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

    for (; start_pc <= end_pc; start_pc++) {
        block->page_mask |= ((uint64_t) 1 << start_pc);
    }

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
                if (!block->next_2->valid)
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

    if (!(block->flags & CODEBLOCK_HAS_FPU))
        block->flags &= ~CODEBLOCK_STATIC_TOP;
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
// clang-format on

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
        switch (cpu_mod) {
            case 0:
                addbyte(0xa1); /*MOVL *mod1add[0][cpu_rm], %eax*/
                addlong((uint32_t) mod1add[0][cpu_rm]);
                addbyte(0x03); /*ADDL *mod1add[1][cpu_rm], %eax*/
                addbyte(0x05);
                addlong((uint32_t) mod1add[1][cpu_rm]);
                break;
            case 1:
                addbyte(0xb8); /*MOVL ,%eax*/
                addlong((uint32_t) (int8_t) (rmdat >> 8));
                addbyte(0x03); /*ADDL *mod1add[0][cpu_rm], %eax*/
                addbyte(0x05);
                addlong((uint32_t) mod1add[0][cpu_rm]);
                addbyte(0x03); /*ADDL *mod1add[1][cpu_rm], %eax*/
                addbyte(0x05);
                addlong((uint32_t) mod1add[1][cpu_rm]);
                (*op_pc)++;
                break;
            case 2:
                addbyte(0xb8); /*MOVL ,%eax*/
                addlong((fetchdat >> 8) & 0xffff);
                addbyte(0x03); /*ADDL *mod1add[0][cpu_rm], %eax*/
                addbyte(0x05);
                addlong((uint32_t) mod1add[0][cpu_rm]);
                addbyte(0x03); /*ADDL *mod1add[1][cpu_rm], %eax*/
                addbyte(0x05);
                addlong((uint32_t) mod1add[1][cpu_rm]);
                (*op_pc) += 2;
                break;
        }
        addbyte(0x25); /*ANDL $0xffff, %eax*/
        addlong(0xffff);
        addbyte(0xa3);
        addlong((uint32_t) &cpu_state.eaaddr);

        if (mod1seg[cpu_rm] == &ss && !op_ssegs)
            op_ea_seg = &cpu_state.seg_ss;
    }
    return op_ea_seg;
}

static x86seg *
codegen_generate_ea_32_long(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, int stack_offset)
{
    uint32_t new_eaaddr;

    if (cpu_rm == 4) {
        uint8_t sib = fetchdat >> 8;
        (*op_pc)++;

        switch (cpu_mod) {
            case 0:
                if ((sib & 7) == 5) {
                    new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                    addbyte(0xb8); /*MOVL ,%eax*/
                    addlong(new_eaaddr);
                    (*op_pc) += 4;
                } else {
                    addbyte(0x8b); /*MOVL regs[sib&7].l, %eax*/
                    addbyte(0x45);
                    addbyte((uint8_t) cpu_state_offset(regs[sib & 7].l));
                }
                break;
            case 1:
                new_eaaddr = (uint32_t) (int8_t) ((fetchdat >> 16) & 0xff);
                addbyte(0xb8); /*MOVL new_eaaddr, %eax*/
                addlong(new_eaaddr);
                addbyte(0x03); /*ADDL regs[sib&7].l, %eax*/
                addbyte(0x45);
                addbyte((uint8_t) cpu_state_offset(regs[sib & 7].l));
                (*op_pc)++;
                break;
            case 2:
                new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                addbyte(0xb8); /*MOVL new_eaaddr, %eax*/
                addlong(new_eaaddr);
                addbyte(0x03); /*ADDL regs[sib&7].l, %eax*/
                addbyte(0x45);
                addbyte((uint8_t) cpu_state_offset(regs[sib & 7].l));
                (*op_pc) += 4;
                break;
        }
        if (stack_offset && (sib & 7) == 4 && (cpu_mod || (sib & 7) != 5)) /*ESP*/
        {
            addbyte(0x05);
            addlong(stack_offset);
        }
        if (((sib & 7) == 4 || (cpu_mod && (sib & 7) == 5)) && !op_ssegs)
            op_ea_seg = &cpu_state.seg_ss;
        if (((sib >> 3) & 7) != 4) {
            switch (sib >> 6) {
                case 0:
                    addbyte(0x03); /*ADDL regs[sib&7].l, %eax*/
                    addbyte(0x45);
                    addbyte((uint8_t) cpu_state_offset(regs[(sib >> 3) & 7].l));
                    break;
                case 1:
                    addbyte(0x8B);
                    addbyte(0x5D);
                    addbyte((uint8_t) cpu_state_offset(regs[(sib >> 3) & 7].l)); /*MOVL armregs[RD],%ebx*/
                    addbyte(0x01);
                    addbyte(0xD8); /*ADDL %ebx,%eax*/
                    addbyte(0x01);
                    addbyte(0xD8); /*ADDL %ebx,%eax*/
                    break;
                case 2:
                    addbyte(0x8B);
                    addbyte(0x5D);
                    addbyte((uint8_t) cpu_state_offset(regs[(sib >> 3) & 7].l)); /*MOVL armregs[RD],%ebx*/
                    addbyte(0xC1);
                    addbyte(0xE3);
                    addbyte(2); /*SHL $2,%ebx*/
                    addbyte(0x01);
                    addbyte(0xD8); /*ADDL %ebx,%eax*/
                    break;
                case 3:
                    addbyte(0x8B);
                    addbyte(0x5D);
                    addbyte((uint8_t) cpu_state_offset(regs[(sib >> 3) & 7].l)); /*MOVL armregs[RD],%ebx*/
                    addbyte(0xC1);
                    addbyte(0xE3);
                    addbyte(3); /*SHL $2,%ebx*/
                    addbyte(0x01);
                    addbyte(0xD8); /*ADDL %ebx,%eax*/
                    break;
            }
        }
        addbyte(0xa3);
        addlong((uint32_t) &cpu_state.eaaddr);
    } else {
        if (!cpu_mod && cpu_rm == 5) {
            new_eaaddr = fastreadl(cs + (*op_pc) + 1);
            addbyte(0xC7); /*MOVL $new_eaaddr,(eaaddr)*/
            addbyte(0x45);
            addbyte((uint8_t) cpu_state_offset(eaaddr));
            addlong(new_eaaddr);
            (*op_pc) += 4;
            return op_ea_seg;
        }
        addbyte(0x8b); /*MOVL regs[sib&7].l, %eax*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(regs[cpu_rm].l));
        cpu_state.eaaddr = cpu_state.regs[cpu_rm].l;
        if (cpu_mod) {
            if (cpu_rm == 5 && !op_ssegs)
                op_ea_seg = &cpu_state.seg_ss;
            if (cpu_mod == 1) {
                addbyte(0x05);
                addlong((uint32_t) (int8_t) (fetchdat >> 8));
                (*op_pc)++;
            } else {
                new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                addbyte(0x05);
                addlong(new_eaaddr);
                (*op_pc) += 4;
            }
        }
        addbyte(0xa3);
        addlong((uint32_t) &cpu_state.eaaddr);
    }
    return op_ea_seg;
}

void
codegen_generate_call(uint8_t opcode, OpFn op, uint32_t fetchdat, uint32_t new_pc, uint32_t old_pc)
{
    codeblock_t *block           = &codeblock[block_current];
    uint32_t     op_32           = use32;
    uint32_t     op_pc           = new_pc;
    const OpFn  *op_table        = x86_dynarec_opcodes;
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
    mmx_ebx_ecx_loaded = 0;
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
            addbyte(0x05);
            addlong((uint32_t) (uintptr_t) &pic_pending);
            addbyte(0x01);
            addbyte(0x0F);
            addbyte(0x85); /*JNZ 0*/
            addlong((uint32_t) &block->data[BLOCK_EXIT_OFFSET] - (uint32_t) (&block->data[block_pos + 4]));
#    endif

            return;
        }
    }

    op = op_table[((opcode >> opcode_shift) | op_32) & opcode_mask];
    if (op_ssegs != last_ssegs) {
        last_ssegs = op_ssegs;

        addbyte(0xC6); /*MOVB [ssegs],op_ssegs*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(ssegs));
        addbyte(op_pc + pc_off);
    }

    if (!test_modrm || (op_table == x86_dynarec_opcodes && opcode_modrm[opcode]) || (op_table == x86_dynarec_opcodes_0f && opcode_0f_modrm[opcode])) {
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
        last_ea_seg = op_ea_seg;
        addbyte(0xC7); /*MOVL $&cpu_state.seg_ds,(ea_seg)*/
        addbyte(0x45);
        addbyte((uint8_t) cpu_state_offset(ea_seg));
        addlong((uint32_t) op_ea_seg);
    }

    codegen_accumulate_flush();

    addbyte(0xC7); /*MOVL pc,new_pc*/
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

    addbyte(0xC7); /*MOVL $fetchdat,(%esp)*/
    addbyte(0x04);
    addbyte(0x24);
    addlong(fetchdat);

    addbyte(0xE8); /*CALL*/
    addlong(((uint8_t *) op - (uint8_t *) (&block->data[block_pos + 4])));

    codegen_block_ins++;

    block->ins++;

#    ifdef CHECK_INT
    /* Check for interrupts. */
    addbyte(0x0a); /* or  al,byte ptr[&pic_pending] */
    addbyte(0x05);
    addlong((uint32_t) (uintptr_t) &pic_pending);
#    endif

    addbyte(0x09); /*OR %eax, %eax*/
    addbyte(0xc0);
    addbyte(0x0F);
    addbyte(0x85); /*JNZ 0*/
    addlong((uint32_t) &block->data[BLOCK_EXIT_OFFSET] - (uint32_t) (&block->data[block_pos + 4]));

    codegen_endpc = (cs + cpu_state.pc) + 8;
}

#endif
