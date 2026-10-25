#if defined __aarch64__ || defined _M_ARM64

#    include <inttypes.h>
#    include <stddef.h>
#    include <stdlib.h>
#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_arm64_defs.h"
#    include "codegen_backend_arm64_ops.h"
#    include "codegen_ir_defs.h"
#    include "codegen_link.h"
#    include "codegen_ops_rep.h"
#    include "codegen_reg.h"
#    include "x86.h"
#    include "x86seg_common.h"
#    include "x86seg.h"
#    include "x87_sf.h"
#    include "x87.h"
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
#        include <stddef.h>
#        include <86box/nmi.h>
#        include <86box/pic.h>
#        include <86box/timer.h>
#        include "x86_interrupt.h"
#    endif

#    if defined(__linux__) || defined(__APPLE__)
#        include <sys/mman.h>
#        include <unistd.h>
#    endif
#    if defined WIN32 || defined _WIN32 || defined _WIN32
#        include <windows.h>
#    endif
#    include <string.h>

void *codegen_mem_load_byte;
void *codegen_mem_load_word;
void *codegen_mem_load_long;
void *codegen_mem_load_word_ea;
void *codegen_mem_load_long_ea;
void *codegen_mem_load_quad;
void *codegen_mem_load_single;
void *codegen_mem_load_double;

void *codegen_mem_store_byte;
void *codegen_mem_store_word;
void *codegen_mem_store_long;
void *codegen_mem_store_quad;
void *codegen_mem_store_single;
void *codegen_mem_store_double;

void *codegen_fp_round;
void *codegen_fp_round_quad;

void *codegen_gpf_rout;
void *codegen_ss_rout;
void *codegen_exit_rout;
void *codegen_rep_rout[2][3][2];

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
codegen_link_slot_t codegen_link_slots[BLOCK_SIZE][CODEGEN_LINK_SLOTS];
#    endif
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
static void *codegen_link_rout;
/*The block epilogue that returns X0 as it is*/
static void *codegen_link_miss_rout;
/*Exits emitted so far for the block being compiled; each gets its own slot*/
static int link_site;
#    endif

host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS] = {
    { REG_X19, 0},
    { REG_X20, 0},
    { REG_X21, 0},
    { REG_X22, 0},
    { REG_X23, 0},
    { REG_X24, 0},
    { REG_X25, 0},
    { REG_X26, 0},
    { REG_X27, 0},
    { REG_X28, 0}
};

host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] = {
    { REG_V8,  0},
    { REG_V9,  0},
    { REG_V10, 0},
    { REG_V11, 0},
    { REG_V12, 0},
    { REG_V13, 0},
    { REG_V14, 0},
    { REG_V15, 0}
};

/*ea_rule: take the direct load whenever the address is at most 0xffc into
  its page, aligned or not, as the interpreter's eal_r path does; readmemwl
  and readmemll charge timing_misaligned only on the slow path.*/
static void
build_load_routine(codeblock_t *block, int size, int is_float, int ea_rule)
{
    uint32_t *branch_offset;
    uint32_t *misaligned_offset;

    /*In - W0 = address
      Out - W0 = data, W1 = abrt*/
    /*MOV W1, W0, LSR #12
      MOV X2, #readlookup2
      LDR X1, [X2, X1, LSL #3]
      CMP X1, #-1
      BEQ +
      LDRB W0, [X1, X0]
      MOV W1, #0
      RET
    * STP X29, X30, [SP, #-16]
      BL readmembl
      LDRB R1, cpu_state.abrt
      LDP X29, X30, [SP, #-16]
      RET
    */
    codegen_alloc(block, 80);
    host_arm64_MOV_REG_LSR(block, REG_W1, REG_W0, 12);
    host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) readlookup2);
    host_arm64_LDRX_REG_LSL3(block, REG_X1, REG_X2, REG_X1);
    if (ea_rule) {
        host_arm64_AND_IMM(block, REG_W2, REG_W0, 0xfff);
        host_arm64_CMP_IMM(block, REG_W2, 0xffc);
        misaligned_offset = host_arm64_BHI_(block);
    } else if (size != 1) {
        host_arm64_TST_IMM(block, REG_W0, size - 1);
        misaligned_offset = host_arm64_BNE_(block);
    }
    host_arm64_CMPX_IMM(block, REG_X1, -1);
    branch_offset = host_arm64_BEQ_(block);
    if (size == 1 && !is_float)
        host_arm64_LDRB_REG(block, REG_W0, REG_W1, REG_W0);
    else if (size == 2 && !is_float)
        host_arm64_LDRH_REG(block, REG_W0, REG_W1, REG_W0);
    else if (size == 4 && !is_float)
        host_arm64_LDR_REG(block, REG_W0, REG_W1, REG_W0);
    else if (size == 4 && is_float)
        host_arm64_LDR_REG_F32(block, REG_V_TEMP, REG_W1, REG_W0);
    else if (size == 8)
        host_arm64_LDR_REG_F64(block, REG_V_TEMP, REG_W1, REG_W0);
    host_arm64_MOVZ_IMM(block, REG_W1, 0);
    host_arm64_RET(block, REG_X30);

    host_arm64_branch_set_offset(branch_offset, &block_write_data[block_pos]);
    if (size != 1)
        host_arm64_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    if (size == 1)
        host_arm64_call(block, (void *) readmembl);
    else if (size == 2)
        host_arm64_call(block, (void *) readmemwl);
    else if (size == 4)
        host_arm64_call(block, (void *) readmemll);
    else if (size == 8)
        host_arm64_call(block, (void *) readmemql);
    else
        fatal("build_load_routine - unknown size %i\n", size);
    codegen_direct_read_8(block, REG_W1, &cpu_state.abrt);
    if (size == 4 && is_float)
        host_arm64_FMOV_S_W(block, REG_V_TEMP, REG_W0);
    else if (size == 8)
        host_arm64_FMOV_D_Q(block, REG_V_TEMP, REG_X0);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);
}

#    if !defined(USE_GDBSTUB)
/*An aligned dword store to a RAM page whose writes are tracked (page_lookup):
  what writememll() and mem_write_raml_page() do, without the two calls. The
  dword never reaches the next 64-byte chunk, so only one dirty bit and one
  byte-mask word change. writememll()'s addr64a[] is left alone: every user
  fills it before reading it. Anything else returns through to_c[] to the C
  path.
  In - W0 = address, W1 = data. Returns the number of to_c[] branches.*/
static int
build_store_long_tracked(codeblock_t *block, uint32_t **to_c)
{
    uint32_t *done[3];
    uint32_t *add_evict;
    int       nr_to_c = 0;

    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) &codegen_in_recompile);
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X3, 0);
    host_arm64_CMP_IMM(block, REG_W3, 0);
    to_c[nr_to_c++] = host_arm64_BNE_(block);
    /*An armed debug breakpoint needs writememll()'s check*/
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) &cpu_dr_armed);
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X3, 0);
    host_arm64_CMP_IMM(block, REG_W3, 0);
    to_c[nr_to_c++] = host_arm64_BNE_(block);

    /*X4 = page*/
    host_arm64_MOV_REG_LSR(block, REG_W2, REG_W0, 12);
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) page_lookup);
    host_arm64_LDRX_REG_LSL3(block, REG_X4, REG_X3, REG_X2);
    host_arm64_CMPX_IMM(block, REG_X4, 0);
    to_c[nr_to_c++] = host_arm64_BEQ_(block);
    host_arm64_LDR_IMM_X(block, REG_X5, REG_X4, offsetof(page_t, write_l));
    host_arm64_MOVX_IMM(block, REG_X6, (uint64_t) mem_write_raml_page);
    host_arm64_CMPX_REG(block, REG_X5, REG_X6);
    to_c[nr_to_c++] = host_arm64_BNE_(block);

    /*X5 = page->mem*/
    host_arm64_LDR_IMM_X(block, REG_X5, REG_X4, offsetof(page_t, mem));
    host_arm64_CMPX_IMM(block, REG_X5, 0);
    to_c[nr_to_c++] = host_arm64_BEQ_(block);
    host_arm64_MOVX_IMM(block, REG_X6, (uint64_t) page_ff);
    host_arm64_CMPX_REG(block, REG_X5, REG_X6);
    to_c[nr_to_c++] = host_arm64_BEQ_(block);

    host_arm64_MOVX_IMM(block, REG_X6, (uint64_t) &mem_logical_addr);
    host_arm64_STR_IMM_W(block, REG_W0, REG_X6, 0);
    host_arm64_MOVX_IMM(block, REG_X6, (uint64_t) &high_page);
    host_arm64_STRB_IMM(block, REG_WZR, REG_X6, 0);

    /*Unchanged data: no store and no dirty bits*/
    host_arm64_AND_IMM(block, REG_W6, REG_W0, 0xfff);
    host_arm64_LDR_REG(block, REG_W7, REG_X5, REG_X6);
    host_arm64_CMP_REG(block, REG_W7, REG_W1);
    done[0] = host_arm64_BEQ_(block);
    host_arm64_STR_REG(block, REG_W1, REG_X5, REG_X6);

    /*W6 = 64-byte chunk, X7 = its dirty_mask bit*/
    host_arm64_UBFX(block, REG_W6, REG_W0, 6, 6);
    host_arm64_MOVZ_IMM(block, REG_X7, 1);
    host_arm64_LSLX(block, REG_X7, REG_X7, REG_X6);
    host_arm64_LDR_IMM_X(block, REG_X8, REG_X4, offsetof(page_t, dirty_mask));
    host_arm64_ORRX_REG(block, REG_X8, REG_X8, REG_X7, 0);
    host_arm64_STR_IMM_Q(block, REG_X8, REG_X4, offsetof(page_t, dirty_mask));

    /*W9 = byte in chunk, X10 = byte_mask, W13 = chunk word offset*/
    host_arm64_AND_IMM(block, REG_W9, REG_W0, 63);
    host_arm64_MOVZ_IMM(block, REG_X10, 0xf);
    host_arm64_LSLX(block, REG_X10, REG_X10, REG_X9);
    host_arm64_MOV_REG(block, REG_W13, REG_W6, 3);
    host_arm64_LDR_IMM_X(block, REG_X11, REG_X4, offsetof(page_t, byte_dirty_mask));
    host_arm64_ADDX_REG_UXTW(block, REG_X11, REG_X11, REG_W13);
    host_arm64_LDR_IMM_X(block, REG_X12, REG_X11, 0);
    host_arm64_ORRX_REG(block, REG_X12, REG_X12, REG_X10, 0);
    host_arm64_STR_IMM_Q(block, REG_X12, REG_X11, 0);

    /*A page not yet on the evict list goes on it when the store hits code*/
    host_arm64_LDR_IMM_W(block, REG_W12, REG_X4, offsetof(page_t, evict_prev));
    host_arm64_CMN_IMM(block, REG_W12, 1);
    done[1] = host_arm64_BNE_(block);
    host_arm64_LDR_IMM_X(block, REG_X12, REG_X4, offsetof(page_t, code_present_mask));
    host_arm64_LSRX(block, REG_X12, REG_X12, REG_X6);
    host_arm64_TST_IMM(block, REG_W12, 1);
    add_evict = host_arm64_BNE_(block);
    host_arm64_LDR_IMM_X(block, REG_X12, REG_X4, offsetof(page_t, byte_code_present_mask));
    host_arm64_LDR_REG_X(block, REG_X12, REG_X12, REG_X13);
    host_arm64_LSRX(block, REG_X12, REG_X12, REG_X9);
    host_arm64_TST_IMM(block, REG_W12, 0xf);
    done[2] = host_arm64_BEQ_(block);
    host_arm64_branch_set_offset(add_evict, &block_write_data[block_pos]);
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    host_arm64_MOVX_REG(block, REG_X0, REG_X4, 0);
    host_arm64_call(block, (void *) page_add_to_evict_list);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);

    for (int c = 0; c < 3; c++)
        host_arm64_branch_set_offset(done[c], &block_write_data[block_pos]);
    host_arm64_MOVZ_IMM(block, REG_X1, 0);
    host_arm64_RET(block, REG_X30);

    return nr_to_c;
}
#    endif

static void
build_store_routine(codeblock_t *block, int size, int is_float)
{
    uint32_t *branch_offset;
    uint32_t *misaligned_offset;

    /*In - R0 = address, R1 = data
      Out - R1 = abrt*/
    /*MOV W2, W0, LSR #12
      MOV X3, #writelookup2
      LDR X2, [X3, X2, LSL #3]
      CMP X2, #-1
      BEQ +
      STRB W1, [X2, X0]
      MOV W1, #0
      RET
    * STP X29, X30, [SP, #-16]
      BL writemembl
      LDRB R1, cpu_state.abrt
      LDP X29, X30, [SP, #-16]
      RET
    */
    codegen_alloc(block, 80);
    host_arm64_MOV_REG_LSR(block, REG_W2, REG_W0, 12);
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) writelookup2);
    host_arm64_LDRX_REG_LSL3(block, REG_X2, REG_X3, REG_X2);
    if (size != 1) {
        host_arm64_TST_IMM(block, REG_W0, size - 1);
        misaligned_offset = host_arm64_BNE_(block);
    }
    host_arm64_CMPX_IMM(block, REG_X2, -1);
    branch_offset = host_arm64_BEQ_(block);
    if (size == 1 && !is_float)
        host_arm64_STRB_REG(block, REG_X1, REG_X2, REG_X0);
    else if (size == 2 && !is_float)
        host_arm64_STRH_REG(block, REG_X1, REG_X2, REG_X0);
    else if (size == 4 && !is_float)
        host_arm64_STR_REG(block, REG_X1, REG_X2, REG_X0);
    else if (size == 4 && is_float)
        host_arm64_STR_REG_F32(block, REG_V_TEMP, REG_X2, REG_X0);
    else if (size == 8)
        host_arm64_STR_REG_F64(block, REG_V_TEMP, REG_X2, REG_X0);
    host_arm64_MOVZ_IMM(block, REG_X1, 0);
    host_arm64_RET(block, REG_X30);

    host_arm64_branch_set_offset(branch_offset, &block_write_data[block_pos]);
#    if !defined(USE_GDBSTUB)
    if (size == 4 && !is_float) {
        uint32_t *to_c[6];
        int       nr_to_c = build_store_long_tracked(block, to_c);

        for (int c = 0; c < nr_to_c; c++)
            host_arm64_branch_set_offset(to_c[c], &block_write_data[block_pos]);
    }
#    endif
    if (size != 1)
        host_arm64_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    if (size == 4 && is_float)
        host_arm64_FMOV_W_S(block, REG_W1, REG_V_TEMP);
    else if (size == 8)
        host_arm64_FMOV_Q_D(block, REG_X1, REG_V_TEMP);
    if (size == 1)
        host_arm64_call(block, (void *) writemembl);
    else if (size == 2)
        host_arm64_call(block, (void *) writememwl);
    else if (size == 4)
        host_arm64_call(block, (void *) writememll);
    else if (size == 8)
        host_arm64_call(block, (void *) writememql);
    else
        fatal("build_store_routine - unknown size %i\n", size);
    codegen_direct_read_8(block, REG_W1, &cpu_state.abrt);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);
}

static void
build_loadstore_routines(codeblock_t *block)
{
    codegen_mem_load_byte = &block_write_data[block_pos];
    build_load_routine(block, 1, 0, 0);
    codegen_mem_load_word = &block_write_data[block_pos];
    build_load_routine(block, 2, 0, 0);
    codegen_mem_load_long = &block_write_data[block_pos];
    build_load_routine(block, 4, 0, 0);
    codegen_mem_load_quad = &block_write_data[block_pos];
    build_load_routine(block, 8, 0, 0);
    codegen_mem_load_single = &block_write_data[block_pos];
    build_load_routine(block, 4, 1, 0);
    codegen_mem_load_double = &block_write_data[block_pos];
    build_load_routine(block, 8, 1, 0);
    codegen_mem_load_word_ea = &block_write_data[block_pos];
    build_load_routine(block, 2, 0, 1);
    codegen_mem_load_long_ea = &block_write_data[block_pos];
    build_load_routine(block, 4, 0, 1);

    codegen_mem_store_byte = &block_write_data[block_pos];
    build_store_routine(block, 1, 0);
    codegen_mem_store_word = &block_write_data[block_pos];
    build_store_routine(block, 2, 0);
    codegen_mem_store_long = &block_write_data[block_pos];
    build_store_routine(block, 4, 0);
    codegen_mem_store_quad = &block_write_data[block_pos];
    build_store_routine(block, 8, 0);
    codegen_mem_store_single = &block_write_data[block_pos];
    build_store_routine(block, 4, 1);
    codegen_mem_store_double = &block_write_data[block_pos];
    build_store_routine(block, 8, 1);
}

static void
build_fp_round_routine(codeblock_t *block, int is_quad)
{
    uint64_t *jump_table;

    codegen_alloc(block, 80);
    host_arm64_LDR_IMM_W(block, REG_TEMP, REG_CPUSTATE, (uintptr_t) &cpu_state.new_fp_control - (uintptr_t) &cpu_state);
    host_arm64_ADR(block, REG_TEMP2, 12);
    host_arm64_LDR_REG_X(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
    host_arm64_BR(block, REG_TEMP2);

    jump_table = (uint64_t *) &block_write_data[block_pos];
    block_pos += 4 * 8;

    jump_table[X87_ROUNDING_NEAREST] = (uint64_t) (uintptr_t) &block_write_data[block_pos]; // tie even
    if (is_quad)
        host_arm64_FCVTNS_X_D(block, REG_TEMP, REG_V_TEMP);
    else
        host_arm64_FCVTNS_W_D(block, REG_TEMP, REG_V_TEMP);
    host_arm64_RET(block, REG_X30);

    jump_table[X87_ROUNDING_UP] = (uint64_t) (uintptr_t) &block_write_data[block_pos]; // pos inf
    if (is_quad)
        host_arm64_FCVTPS_X_D(block, REG_TEMP, REG_V_TEMP);
    else
        host_arm64_FCVTPS_W_D(block, REG_TEMP, REG_V_TEMP);
    host_arm64_RET(block, REG_X30);

    jump_table[X87_ROUNDING_DOWN] = (uint64_t) (uintptr_t) &block_write_data[block_pos]; // neg inf
    if (is_quad)
        host_arm64_FCVTMS_X_D(block, REG_TEMP, REG_V_TEMP);
    else
        host_arm64_FCVTMS_W_D(block, REG_TEMP, REG_V_TEMP);
    host_arm64_RET(block, REG_X30);

    jump_table[X87_ROUNDING_CHOP] = (uint64_t) (uintptr_t) &block_write_data[block_pos]; // zero
    if (is_quad)
        host_arm64_FCVTZS_X_D(block, REG_TEMP, REG_V_TEMP);
    else
        host_arm64_FCVTZS_W_D(block, REG_TEMP, REG_V_TEMP);
    host_arm64_RET(block, REG_X30);
}

#    define REP_SEG     REG_X9
#    define REP_CNT     REG_W10
#    define REP_SRC     REG_W11
#    define REP_VAL     REG_X11
#    define REP_DST     REG_W12
#    define REP_CYCLES  REG_W13
#    define REP_END     REG_W14
#    define REP_STEP    REG_W15
#    define REP_ELEMCYC REG_W17
#    define REP_SLOW    REG_W8
#    define REP_RL      REG_X3
#    define REP_WL      REG_X4

#    define REP_MAX_PATCH 24

static int
rep_state_offset(void *p)
{
    return (int) ((uintptr_t) p - (uintptr_t) &cpu_state);
}

static void
rep_patch_list(uint32_t **list, int nr, void *dest)
{
    for (int c = 0; c < nr; c++)
        host_arm64_branch_set_offset(list[c], dest);
}

static void
rep_writeback(codeblock_t *block, int is_stos, int a32)
{
    if (a32) {
        host_arm64_STR_IMM_W(block, REP_CNT, REG_CPUSTATE, rep_state_offset(&ECX));
        if (!is_stos)
            host_arm64_STR_IMM_W(block, REP_SRC, REG_CPUSTATE, rep_state_offset(&ESI));
        host_arm64_STR_IMM_W(block, REP_DST, REG_CPUSTATE, rep_state_offset(&EDI));
    } else {
        host_arm64_STRH_IMM(block, REP_CNT, REG_CPUSTATE, rep_state_offset(&CX));
        if (!is_stos)
            host_arm64_STRH_IMM(block, REP_SRC, REG_CPUSTATE, rep_state_offset(&SI));
        host_arm64_STRH_IMM(block, REP_DST, REG_CPUSTATE, rep_state_offset(&DI));
    }
    host_arm64_STR_IMM_W(block, REP_CYCLES, REG_CPUSTATE, rep_state_offset(&cpu_state._cycles));
}

/*Sets W6 to 1 if the segment at base_reg + offset would fail the interpreter's
  null selector or not-present check, using W2 = (msw & 1) && !(eflags & VM)*/
static void
rep_seg_check(codeblock_t *block, int base_reg, int offset)
{
    host_arm64_LDR_IMM_W(block, REG_W5, base_reg, offset + offsetof(x86seg, base));
    host_arm64_CMN_IMM(block, REG_W5, 1);
    host_arm64_CSET_NE(block, REG_W6);
    host_arm64_EOR_IMM(block, REG_W6, REG_W6, 1);
    host_arm64_ORR_REG(block, REP_SLOW, REP_SLOW, REG_W6, 0);
    host_arm64_LDRB_IMM_W(block, REG_W5, base_reg, offset + offsetof(x86seg, access));
    host_arm64_TST_IMM(block, REG_W5, 0x80);
    host_arm64_CSEL_EQ(block, REG_W6, REG_W2, REG_WZR);
    host_arm64_ORR_REG(block, REP_SLOW, REP_SLOW, REG_W6, 0);
}

/*Clamps W0 to W1 (unsigned)*/
static void
rep_min(codeblock_t *block)
{
    host_arm64_CMP_REG(block, REG_W0, REG_W1);
    host_arm64_CSEL_CC(block, REG_W0, REG_W0, REG_W1);
}

/*Forward run: W1 = elements left in the page at lin_reg*/
static void
rep_page_room_fwd(codeblock_t *block, int lin_reg, int shift)
{
    host_arm64_EOR_IMM(block, REG_W1, lin_reg, 0xfff);
    host_arm64_AND_IMM(block, REG_W1, REG_W1, 0xfff);
    host_arm64_ADD_IMM(block, REG_W1, REG_W1, 1);
    if (shift)
        host_arm64_MOV_REG_LSR(block, REG_W1, REG_W1, shift);
    rep_min(block);
}

/*Backward run: W1 = elements down to the start of the page at lin_reg*/
static void
rep_page_room_bwd(codeblock_t *block, int lin_reg, int shift)
{
    host_arm64_AND_IMM(block, REG_W1, lin_reg, 0xfff);
    if (shift)
        host_arm64_MOV_REG_LSR(block, REG_W1, REG_W1, shift);
    host_arm64_ADD_IMM(block, REG_W1, REG_W1, 1);
    rep_min(block);
}

/*Forward run: elements before the offset in off_reg wraps. Zero if the
  first element already straddles the wrap.*/
static void
rep_offset_room_fwd(codeblock_t *block, int off_reg, int shift, int a32)
{
    if (a32) {
        host_arm64_SUB_REG(block, REG_W1, REG_WZR, off_reg, 0);
        if (shift)
            host_arm64_MOV_REG_LSR(block, REG_W1, REG_W1, shift);
        /*Offset 0 has the whole 4 GB ahead of it*/
        host_arm64_CMP_IMM(block, off_reg, 0);
        host_arm64_CSEL_EQ(block, REG_W1, REP_CNT, REG_W1);
    } else {
        host_arm64_mov_imm(block, REG_W1, 0x10000);
        host_arm64_SUB_REG(block, REG_W1, REG_W1, off_reg, 0);
        if (shift)
            host_arm64_MOV_REG_LSR(block, REG_W1, REG_W1, shift);
    }
    rep_min(block);
}

/*Backward run: elements down to offset 0, and a branch to the slow path if
  the first element straddles the top of the offset range*/
static void
rep_offset_room_bwd(codeblock_t *block, int off_reg, int shift, int a32, uint32_t **slow, int *nr_slow)
{
    int size = 1 << shift;

    if (shift)
        host_arm64_MOV_REG_LSR(block, REG_W1, off_reg, shift);
    else
        host_arm64_MOV_REG(block, REG_W1, off_reg, 0);
    host_arm64_ADD_IMM(block, REG_W1, REG_W1, 1);
    rep_min(block);
    if (size > 1) {
        if (a32) {
            host_arm64_CMN_IMM(block, off_reg, size - 1);
            slow[(*nr_slow)++] = host_arm64_BCS_(block);
        } else {
            host_arm64_ADD_IMM(block, REG_W1, off_reg, size - 1);
            host_arm64_MOV_REG_LSR(block, REG_W1, REG_W1, 16);
            host_arm64_CMP_IMM(block, REG_W1, 0);
            slow[(*nr_slow)++] = host_arm64_BNE_(block);
        }
    }
}

/*Branches to the slow path unless every element of the run lies inside the
  segment's limits. W1 = run length in bytes, W15 = step.*/
static void
rep_limit_check(codeblock_t *block, int off_reg, int base_reg, int seg_offset, int size, uint32_t **slow, int *nr_slow)
{
    uint32_t *bwd;
    uint32_t *done;

    bwd = host_arm64_TBNZ(block, REP_STEP, 31);
    host_arm64_LDR_IMM_W(block, REG_W7, base_reg, seg_offset + offsetof(x86seg, limit_low));
    host_arm64_CMP_REG(block, off_reg, REG_W7);
    slow[(*nr_slow)++] = host_arm64_BCC_(block);
    host_arm64_ADD_REG(block, REG_W2, off_reg, REG_W1, 0);
    host_arm64_SUB_IMM(block, REG_W2, REG_W2, 1);
    host_arm64_LDR_IMM_W(block, REG_W7, base_reg, seg_offset + offsetof(x86seg, limit_high));
    host_arm64_CMP_REG(block, REG_W2, REG_W7);
    slow[(*nr_slow)++] = host_arm64_BHI_(block);
    done = host_arm64_B_(block);

    host_arm64_branch_set_offset(bwd, &block_write_data[block_pos]);
    host_arm64_SUB_REG(block, REG_W2, off_reg, REG_W1, 0);
    host_arm64_ADD_IMM(block, REG_W2, REG_W2, size);
    host_arm64_LDR_IMM_W(block, REG_W7, base_reg, seg_offset + offsetof(x86seg, limit_low));
    host_arm64_CMP_REG(block, REG_W2, REG_W7);
    slow[(*nr_slow)++] = host_arm64_BCC_(block);
    host_arm64_ADD_IMM(block, REG_W2, off_reg, size - 1);
    host_arm64_LDR_IMM_W(block, REG_W7, base_reg, seg_offset + offsetof(x86seg, limit_high));
    host_arm64_CMP_REG(block, REG_W2, REG_W7);
    slow[(*nr_slow)++] = host_arm64_BHI_(block);

    host_arm64_branch_set_offset(done, &block_write_data[block_pos]);
}

static void
rep_step_offset(codeblock_t *block, int off_reg, int delta_reg, int a32)
{
    host_arm64_ADD_REG(block, off_reg, off_reg, delta_reg, 0);
    if (!a32)
        host_arm64_AND_IMM(block, off_reg, off_reg, 0xffff);
}

/*REP MOVS and REP STOS.
  In  - W0 = source segment offset in cpu_state (bits 0-11), cycle limit
             (bits 12-23), cycles per element (bits 24-31)
  Out - W0 = 0 to carry on with the block, non-zero to leave it with
             cpu_state.pc set back to the instruction

  Does what the interpreter's REP handlers in x86_ops_rep_dyn.h do: the same
  cycles per element, and the block ends at the same element when the string
  outlasts the cycle limit or the trap flag is set. Elements are copied in
  runs that stay inside one source page and one destination page, both with
  direct lookups, inside both segment limits and inside the cycle budget, so a
  run does exactly what the interpreter's fast path does for each element.
  Any other element goes through elem_func, which is the interpreter's loop
  body for one element.*/
static void
build_rep_routine(codeblock_t *block, int is_stos, int shift, int a32, int (*elem_func)(x86seg *seg))
{
    int       size = 1 << shift;
    int       es_offset = rep_state_offset(&cpu_state.seg_es);
    uint32_t *slow[REP_MAX_PATCH];
    int       nr_slow = 0;
    uint32_t *to_done;
    uint32_t *to_break[2];
    uint32_t *to_abort;
    uint32_t *branch;
    uint32_t *branch2;
    uint8_t  *loop;
    uint8_t  *target;
    uint8_t  *done_ret;

    /*Unpack the parameters*/
    host_arm64_UBFX(block, REG_W1, REG_W0, 12, 12);
    host_arm64_UBFX(block, REP_ELEMCYC, REG_W0, 24, 8);
    if (!is_stos) {
        host_arm64_AND_IMM(block, REG_W0, REG_W0, 0xfff);
        host_arm64_ADDX_REG_UXTW(block, REP_SEG, REG_CPUSTATE, REG_W0);
    }

    /*Guest registers*/
    if (a32) {
        host_arm64_LDR_IMM_W(block, REP_CNT, REG_CPUSTATE, rep_state_offset(&ECX));
        if (!is_stos)
            host_arm64_LDR_IMM_W(block, REP_SRC, REG_CPUSTATE, rep_state_offset(&ESI));
        host_arm64_LDR_IMM_W(block, REP_DST, REG_CPUSTATE, rep_state_offset(&EDI));
    } else {
        host_arm64_LDRH_IMM(block, REP_CNT, REG_CPUSTATE, rep_state_offset(&CX));
        if (!is_stos)
            host_arm64_LDRH_IMM(block, REP_SRC, REG_CPUSTATE, rep_state_offset(&SI));
        host_arm64_LDRH_IMM(block, REP_DST, REG_CPUSTATE, rep_state_offset(&DI));
    }
    host_arm64_LDR_IMM_W(block, REP_CYCLES, REG_CPUSTATE, rep_state_offset(&cpu_state._cycles));

    /*Cycle count at which the loop stops: one element only with the trap flag set*/
    host_arm64_SUB_REG(block, REP_END, REP_CYCLES, REG_W1, 0);
    host_arm64_ADD_IMM(block, REG_W5, REP_CYCLES, 1);
    host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) &trap);
    host_arm64_LDR_IMM_W(block, REG_W2, REG_X2, 0);
    host_arm64_CMP_IMM(block, REG_W2, 0);
    host_arm64_CSEL_EQ(block, REP_END, REP_END, REG_W5);

    /*Direction*/
    host_arm64_LDRH_IMM(block, REG_W2, REG_CPUSTATE, rep_state_offset(&cpu_state.flags));
    host_arm64_mov_imm(block, REP_STEP, size);
    host_arm64_mov_imm(block, REG_W5, -size);
    host_arm64_TST_IMM(block, REG_W2, D_FLAG);
    host_arm64_CSEL_EQ(block, REP_STEP, REP_STEP, REG_W5);

    /*Conditions that send every element down the slow path: an armed debug
      breakpoint, which the interpreter's translate check looks at, and a
      segment that would fault*/
    host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) &cpu_dr_armed);
    host_arm64_LDR_IMM_W(block, REP_SLOW, REG_X2, 0);
    host_arm64_AND_IMM(block, REP_SLOW, REP_SLOW, 0xff);
    host_arm64_LDRH_IMM(block, REG_W2, REG_CPUSTATE, rep_state_offset(&cpu_state.CR0.w));
    host_arm64_AND_IMM(block, REG_W2, REG_W2, 1);
    host_arm64_LDRH_IMM(block, REG_W5, REG_CPUSTATE, rep_state_offset(&cpu_state.eflags));
    host_arm64_TST_IMM(block, REG_W5, VM_FLAG);
    host_arm64_CSEL_EQ(block, REG_W2, REG_W2, REG_WZR);
    rep_seg_check(block, REG_CPUSTATE, es_offset);
    if (!is_stos)
        rep_seg_check(block, REP_SEG, 0);

    /*Lookup tables, and the value to store*/
    if (!is_stos)
        host_arm64_MOVX_IMM(block, REP_RL, (uint64_t) readlookup2);
    host_arm64_MOVX_IMM(block, REP_WL, (uint64_t) writelookup2);
    if (is_stos) {
        if (size == 1) {
            host_arm64_LDRB_IMM_W(block, REG_W11, REG_CPUSTATE, rep_state_offset(&AL));
            host_arm64_ORR_REG(block, REG_W11, REG_W11, REG_W11, 8);
            host_arm64_ORR_REG(block, REG_W11, REG_W11, REG_W11, 16);
        } else if (size == 2) {
            host_arm64_LDRH_IMM(block, REG_W11, REG_CPUSTATE, rep_state_offset(&AX));
            host_arm64_ORR_REG(block, REG_W11, REG_W11, REG_W11, 16);
        } else
            host_arm64_LDR_IMM_W(block, REG_W11, REG_CPUSTATE, rep_state_offset(&EAX));
        host_arm64_ORRX_REG(block, REP_VAL, REP_VAL, REP_VAL, 32);
    }

    /*Top of the loop*/
    loop = &block_write_data[block_pos];
    host_arm64_CMP_IMM(block, REP_CNT, 0);
    to_done = host_arm64_BEQ_(block);
    host_arm64_CMP_IMM(block, REP_SLOW, 0);
    slow[nr_slow++] = host_arm64_BNE_(block);

    /*Linear addresses, W2 = source, W7 = destination*/
    if (!is_stos) {
        host_arm64_LDR_IMM_W(block, REG_W5, REP_SEG, offsetof(x86seg, base));
        host_arm64_ADD_REG(block, REG_W2, REG_W5, REP_SRC, 0);
    }
    host_arm64_LDR_IMM_W(block, REG_W6, REG_CPUSTATE, es_offset + offsetof(x86seg, base));
    host_arm64_ADD_REG(block, REG_W7, REG_W6, REP_DST, 0);
    if (size > 1) {
        /*Misaligned elements take the interpreter's slow path*/
        if (!is_stos) {
            host_arm64_ORR_REG(block, REG_W5, REG_W2, REG_W7, 0);
            host_arm64_TST_IMM(block, REG_W5, size - 1);
        } else
            host_arm64_TST_IMM(block, REG_W7, size - 1);
        slow[nr_slow++] = host_arm64_BNE_(block);
    }

    /*W0 = run length, limited to what is left of each page*/
    host_arm64_MOV_REG(block, REG_W0, REP_CNT, 0);
    branch = host_arm64_TBNZ(block, REP_STEP, 31);
    rep_page_room_fwd(block, REG_W7, shift);
    if (!is_stos)
        rep_page_room_fwd(block, REG_W2, shift);
    branch2 = host_arm64_B_(block);
    host_arm64_branch_set_offset(branch, &block_write_data[block_pos]);
    rep_page_room_bwd(block, REG_W7, shift);
    if (!is_stos)
        rep_page_room_bwd(block, REG_W2, shift);
    host_arm64_branch_set_offset(branch2, &block_write_data[block_pos]);

    /*Direct lookups for both pages, X5 = source and X6 = destination host pointers*/
    if (!is_stos) {
        host_arm64_MOV_REG_LSR(block, REG_W5, REG_W2, 12);
        host_arm64_LDRX_REG_LSL3(block, REG_X5, REP_RL, REG_X5);
        host_arm64_CMPX_IMM(block, REG_X5, -1);
        slow[nr_slow++] = host_arm64_BEQ_(block);
        host_arm64_ADDX_REG_UXTW(block, REG_X5, REG_X5, REG_W2);
    }
    host_arm64_MOV_REG_LSR(block, REG_W6, REG_W7, 12);
    host_arm64_LDRX_REG_LSL3(block, REG_X6, REP_WL, REG_X6);
    host_arm64_CMPX_IMM(block, REG_X6, -1);
    slow[nr_slow++] = host_arm64_BEQ_(block);
    host_arm64_ADDX_REG_UXTW(block, REG_X6, REG_X6, REG_W7);

    /*Limit the run to offsets that do not wrap*/
    branch = host_arm64_TBNZ(block, REP_STEP, 31);
    rep_offset_room_fwd(block, REP_DST, shift, a32);
    if (!is_stos)
        rep_offset_room_fwd(block, REP_SRC, shift, a32);
    branch2 = host_arm64_B_(block);
    host_arm64_branch_set_offset(branch, &block_write_data[block_pos]);
    rep_offset_room_bwd(block, REP_DST, shift, a32, slow, &nr_slow);
    if (!is_stos)
        rep_offset_room_bwd(block, REP_SRC, shift, a32, slow, &nr_slow);
    host_arm64_branch_set_offset(branch2, &block_write_data[block_pos]);

    /*Limit the run to the cycle budget: the element that takes the count
      below the end is the last one*/
    host_arm64_SUB_REG(block, REG_W1, REP_CYCLES, REP_END, 0);
    branch = host_arm64_TBNZ(block, REG_W1, 31);
    host_arm64_UDIV(block, REG_W1, REG_W1, REP_ELEMCYC);
    host_arm64_ADD_IMM(block, REG_W1, REG_W1, 1);
    branch2 = host_arm64_B_(block);
    host_arm64_branch_set_offset(branch, &block_write_data[block_pos]);
    host_arm64_mov_imm(block, REG_W1, 1);
    host_arm64_branch_set_offset(branch2, &block_write_data[block_pos]);
    rep_min(block);
    host_arm64_CMP_IMM(block, REG_W0, 0);
    slow[nr_slow++] = host_arm64_BEQ_(block);

    /*Segment limits for the whole run, W1 = run length in bytes*/
    host_arm64_MOV_REG(block, REG_W1, REG_W0, shift);
    rep_limit_check(block, REP_DST, REG_CPUSTATE, es_offset, size, slow, &nr_slow);
    if (!is_stos)
        rep_limit_check(block, REP_SRC, REP_SEG, 0, size, slow, &nr_slow);

    /*Nothing in the run can fault now, so step the registers past it*/
    host_arm64_SUB_REG(block, REP_CNT, REP_CNT, REG_W0, 0);
    host_arm64_MUL_W(block, REG_W2, REP_STEP, REG_W0);
    rep_step_offset(block, REP_DST, REG_W2, a32);
    if (!is_stos)
        rep_step_offset(block, REP_SRC, REG_W2, a32);
    host_arm64_MUL_W(block, REG_W2, REP_ELEMCYC, REG_W0);
    host_arm64_SUB_REG(block, REP_CYCLES, REP_CYCLES, REG_W2, 0);

    /*Copy. Forward runs move 16 bytes at a time unless the destination
      starts less than 16 bytes after the source, where a later element
      reads what an earlier one wrote*/
    branch = host_arm64_TBNZ(block, REP_STEP, 31);
    if (!is_stos) {
        uint32_t *wide;

        host_arm64_SUBX_REG(block, REG_X2, REG_X6, REG_X5);
        host_arm64_CMPX_IMM(block, REG_X2, 16);
        wide = host_arm64_BCS_(block);
        host_arm64_CMPX_IMM(block, REG_X2, 0);
        branch2 = host_arm64_BNE_(block);
        host_arm64_branch_set_offset(wide, &block_write_data[block_pos]);
    } else
        branch2 = NULL;
    host_arm64_CMP_IMM(block, REG_W1, 16);
    {
        uint32_t *narrow = host_arm64_BCC_(block);
        uint32_t *back;

        target = &block_write_data[block_pos];
        if (!is_stos) {
            host_arm64_LDP_POSTIDX_X(block, REG_X2, REG_X7, REG_X5, 16);
            host_arm64_STP_POSTIDX_X(block, REG_X2, REG_X7, REG_X6, 16);
        } else
            host_arm64_STP_POSTIDX_X(block, REP_VAL, REP_VAL, REG_X6, 16);
        host_arm64_SUB_IMM(block, REG_W1, REG_W1, 16);
        host_arm64_CMP_IMM(block, REG_W1, 16);
        back = host_arm64_BCS_(block);
        host_arm64_branch_set_offset(back, target);

        host_arm64_branch_set_offset(narrow, &block_write_data[block_pos]);
        if (branch2)
            host_arm64_branch_set_offset(branch2, &block_write_data[block_pos]);
    }
    {
        uint32_t *empty;
        uint32_t *back;
        uint32_t *copied;

        host_arm64_CMP_IMM(block, REG_W1, 0);
        empty = host_arm64_BEQ_(block);
        target = &block_write_data[block_pos];
        if (size == 1) {
            if (!is_stos)
                host_arm64_LDRB_POSTIDX(block, REG_W2, REG_X5, 1);
            host_arm64_STRB_POSTIDX(block, is_stos ? REG_W11 : REG_W2, REG_X6, 1);
        } else if (size == 2) {
            if (!is_stos)
                host_arm64_LDRH_POSTIDX(block, REG_W2, REG_X5, 2);
            host_arm64_STRH_POSTIDX(block, is_stos ? REG_W11 : REG_W2, REG_X6, 2);
        } else {
            if (!is_stos)
                host_arm64_LDR_POSTIDX_W(block, REG_W2, REG_X5, 4);
            host_arm64_STR_POSTIDX_W(block, is_stos ? REG_W11 : REG_W2, REG_X6, 4);
        }
        host_arm64_SUB_IMM(block, REG_W1, REG_W1, size);
        host_arm64_CMP_IMM(block, REG_W1, 0);
        back = host_arm64_BNE_(block);
        host_arm64_branch_set_offset(back, target);
        host_arm64_branch_set_offset(empty, &block_write_data[block_pos]);
        copied = host_arm64_B_(block);

        /*Backward runs go one element at a time*/
        host_arm64_branch_set_offset(branch, &block_write_data[block_pos]);
        target = &block_write_data[block_pos];
        if (size == 1) {
            if (!is_stos)
                host_arm64_LDRB_POSTIDX(block, REG_W2, REG_X5, -1);
            host_arm64_STRB_POSTIDX(block, is_stos ? REG_W11 : REG_W2, REG_X6, -1);
        } else if (size == 2) {
            if (!is_stos)
                host_arm64_LDRH_POSTIDX(block, REG_W2, REG_X5, -2);
            host_arm64_STRH_POSTIDX(block, is_stos ? REG_W11 : REG_W2, REG_X6, -2);
        } else {
            if (!is_stos)
                host_arm64_LDR_POSTIDX_W(block, REG_W2, REG_X5, -4);
            host_arm64_STR_POSTIDX_W(block, is_stos ? REG_W11 : REG_W2, REG_X6, -4);
        }
        host_arm64_SUB_IMM(block, REG_W1, REG_W1, size);
        host_arm64_CMP_IMM(block, REG_W1, 0);
        back = host_arm64_BNE_(block);
        host_arm64_branch_set_offset(back, target);
        host_arm64_branch_set_offset(copied, &block_write_data[block_pos]);
    }
    host_arm64_CMP_REG(block, REP_CYCLES, REP_END);
    to_break[0] = host_arm64_BLT_(block);
    host_arm64_B(block, loop);

    /*One element through the interpreter's loop body*/
    rep_patch_list(slow, nr_slow, &block_write_data[block_pos]);
    rep_writeback(block, is_stos, a32);
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X3, REG_X4, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X8, REG_X9, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X10, REG_X11, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X12, REG_X13, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X14, REG_X15, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X17, REG_X1, REG_XSP, -16);
    host_arm64_MOVX_REG(block, REG_X0, REP_SEG, 0);
    host_arm64_call(block, (void *) elem_func);
    host_arm64_LDP_POSTIDX_X(block, REG_X17, REG_X1, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X14, REG_X15, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X12, REG_X13, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X10, REG_X11, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X8, REG_X9, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X3, REG_X4, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    /*A misaligned access charges extra cycles*/
    host_arm64_LDR_IMM_W(block, REP_CYCLES, REG_CPUSTATE, rep_state_offset(&cpu_state._cycles));
    host_arm64_CMP_IMM(block, REG_W0, 0);
    to_abort = host_arm64_BNE_(block);
    if (!is_stos)
        rep_step_offset(block, REP_SRC, REP_STEP, a32);
    rep_step_offset(block, REP_DST, REP_STEP, a32);
    host_arm64_SUB_IMM(block, REP_CNT, REP_CNT, 1);
    host_arm64_SUB_REG(block, REP_CYCLES, REP_CYCLES, REP_ELEMCYC, 0);
    host_arm64_CMP_REG(block, REP_CYCLES, REP_END);
    to_break[1] = host_arm64_BLT_(block);
    host_arm64_B(block, loop);

    /*Count ran out*/
    host_arm64_branch_set_offset(to_done, &block_write_data[block_pos]);
    rep_writeback(block, is_stos, a32);
    done_ret = &block_write_data[block_pos];
    host_arm64_mov_imm(block, REG_W0, 0);
    host_arm64_RET(block, REG_X30);

    /*Out of cycles: carry on if the count ran out on the same element,
      otherwise leave the block to restart the instruction*/
    rep_patch_list(to_break, 2, &block_write_data[block_pos]);
    rep_writeback(block, is_stos, a32);
    host_arm64_CMP_IMM(block, REP_CNT, 0);
    branch = host_arm64_BEQ_(block);
    host_arm64_branch_set_offset(branch, done_ret);
    branch = host_arm64_B_(block);

    /*Faulting element*/
    host_arm64_branch_set_offset(to_abort, &block_write_data[block_pos]);
    rep_writeback(block, is_stos, a32);

    host_arm64_branch_set_offset(branch, &block_write_data[block_pos]);
    host_arm64_LDR_IMM_W(block, REG_W1, REG_CPUSTATE, rep_state_offset(&cpu_state.oldpc));
    host_arm64_STR_IMM_W(block, REG_W1, REG_CPUSTATE, rep_state_offset(&cpu_state.pc));
    host_arm64_mov_imm(block, REG_W0, 1);
    host_arm64_RET(block, REG_X30);

    if (nr_slow > REP_MAX_PATCH)
        fatal("build_rep_routine - too many slow path branches\n");
}

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
/*Literal pool entries, in the pairs the routine loads together with LDP*/
enum {
    LINK_LIT_WAS_RESET,
    LINK_LIT_FLUSH_PENDING,
    LINK_LIT_CPU_INIT,
    LINK_LIT_NEW_NE,
    LINK_LIT_NMI_ENABLE,
    LINK_LIT_FORCE_INTERPRETER,
    LINK_LIT_OVERRIDE_DYNAREC,
    LINK_LIT_NMI,
    LINK_LIT_PIC,
    LINK_LIT_CUR_STATUS,
    LINK_LIT_TSC,
    LINK_LIT_TSC_OLD,
    LINK_LIT_CYCLES_OLD,
    LINK_LIT_TIMER_TARGET,
    LINK_LIT_GET_PHYS_VIRT,
    LINK_LIT_GET_PHYS_PHYS,
    LINK_LIT_OLDCS,
    LINK_LIT_OLDCPL,
    LINK_LIT_FETCH_DEVICE,
    LINK_LIT_CPU_HALTED,
    LINK_LIT_NMI_MASK,
    LINK_LIT_RAMMASK,
    LINK_LIT_READLOOKUP2,
    LINK_LIT_RAM,
    LINK_LIT_DR_ARMED,
    LINK_LIT_IOAPIC_PATH,
    LINK_LIT_LAPIC_EXTINT,
    LINK_LIT_LAPIC_IRR,

    LINK_LIT_COUNT
};

#        define LINK_SLOT(field) ((int) offsetof(codegen_link_slot_t, field))
#        define LINK_LIT(nr)     ((nr) * 8)
#        define LINK_MAX_MISS    32

/*Fields the routine loads together, with LDP or one wider load*/
_Static_assert(offsetof(codegen_link_slot_t, exit_cs) == offsetof(codegen_link_slot_t, exit_pc) + 4
                   && offsetof(codegen_link_slot_t, entry) == offsetof(codegen_link_slot_t, exit_pc) + 8,
               "exit_pc/exit_cs and entry are loaded as one pair");
_Static_assert(offsetof(codegen_link_slot_t, status) == offsetof(codegen_link_slot_t, gen) + 4
                   && offsetof(codegen_link_slot_t, npxc) == offsetof(codegen_link_slot_t, gen) + 6,
               "gen and status/npxc are loaded as one pair");
_Static_assert(offsetof(codegen_link_slot_t, phys_page) == offsetof(codegen_link_slot_t, cr0_bits) + 8
                   && !(offsetof(codegen_link_slot_t, cr0_bits) & 7),
               "cr0_bits and phys_page are the low halves of one pair");
_Static_assert(offsetof(codegen_link_slot_t, xlat_val) == offsetof(codegen_link_slot_t, xlat) + 8
                   && offsetof(codegen_link_slot_t, hash) == offsetof(codegen_link_slot_t, mem_exec) + 8
                   && offsetof(codegen_link_slot_t, dirty_mask) == offsetof(codegen_link_slot_t, page_mask) + 8,
               "pointer fields loaded as pairs");
_Static_assert(offsetof(codegen_link_slot_t, top_mask) == offsetof(codegen_link_slot_t, block_nr) + 2
                   && offsetof(codegen_link_slot_t, top) == offsetof(codegen_link_slot_t, block_nr) + 3
                   && offsetof(codegen_link_slot_t, two_page) == offsetof(codegen_link_slot_t, block_nr) + 4
                   && !(offsetof(codegen_link_slot_t, block_nr) & 7),
               "block_nr, top_mask, top and two_page are loaded as one 64-bit word");
_Static_assert(offsetof(cpu_state_t, _smi_line) == offsetof(cpu_state_t, abrt) + 1
                   && offsetof(cpu_state_t, _cycles) == offsetof(cpu_state_t, abrt) + 2
                   && !((offsetof(cpu_state_t, abrt) - 2) & 3),
               "abrt and smi_line are the top half of the word loaded with _cycles");
_Static_assert(offsetof(cpu_state_t, flags) == offsetof(cpu_state_t, CR0) + 4 && !(offsetof(cpu_state_t, CR0) & 7),
               "CR0 and flags are loaded as one 64-bit word");
_Static_assert(offsetof(x86seg, seg) == offsetof(x86seg, access) + 2 && !(offsetof(cpu_state_t, seg_cs.access) & 3),
               "CS access and selector are loaded as one word");
_Static_assert(T_FLAG == (1 << 8) && I_FLAG == (1 << 9) && CODEBLOCK_BYTE_MASK == (1 << 5), "flag bits tested by number");
_Static_assert(BLOCK_SIZE <= 0x10000, "block numbers and codeblock_hash entries are compared as 16 bits");

/*Entered by a branch from the end of a block, with X0 = the block's slot
  pair and X29 = &cpu_state. Repeats what exec386_dynarec() and
  exec386_dynarec_dyn() do between that block and the next one, assuming
  nothing in that stretch has anything to do: if any check fails it returns
  to C exactly as the block's own epilogue did, before any store. On a hit
  it does the stores the C loop would have done and jumps to the next
  block's link entry, which runs on the frame this block's prologue built.

  Global addresses come from a literal pool placed just before the routine,
  so the routine and the pool must share one allocator block. Loads are
  paired wherever the layout allows, since on a hit the routine is limited
  by how many loads it issues rather than by its length.*/
static void
build_link_routine(codeblock_t *block)
{
    uint64_t *lit;
    uint8_t  *start_data;
    uint32_t *miss[LINK_MAX_MISS];
    int       nr_miss = 0;
    uint32_t *nmi_set;
    uint32_t *nmi_ok;
    uint32_t *irq_ok;
    uint32_t *irq_ok_2;
    uint32_t *es_flat;
    uint32_t *uncached;
    uint32_t *no_paging;
    uint32_t *cached;
    uint32_t *two_page;
    uint32_t *one_page;
    uint32_t *byte_mask;
    uint32_t *no_paging_2;
    uint8_t  *nmi_done;
    uint8_t  *xlat_done;
    uint8_t  *two_page_done;
    uint8_t  *xlat_2_done;
    uint8_t  *miss_dest;

    /*Start a fresh allocator block, so nothing below is split across two*/
    codegen_alloc(block, BLOCK_MAX - 4);
    if (block_pos & 7)
        block_pos += 4;
    start_data = block_write_data;
    lit        = (uint64_t *) &block_write_data[block_pos];
    block_pos += LINK_LIT_COUNT * 8;

    lit[LINK_LIT_WAS_RESET]         = (uint64_t) (uintptr_t) &x86_was_reset;
    lit[LINK_LIT_FLUSH_PENDING]     = (uint64_t) (uintptr_t) &cpu_flush_pending;
    lit[LINK_LIT_CPU_INIT]          = (uint64_t) (uintptr_t) &cpu_init;
    lit[LINK_LIT_NEW_NE]            = (uint64_t) (uintptr_t) &new_ne;
    lit[LINK_LIT_CPU_HALTED]        = (uint64_t) (uintptr_t) &cpu_halted;
    lit[LINK_LIT_FORCE_INTERPRETER] = (uint64_t) (uintptr_t) &cpu_force_interpreter;
    lit[LINK_LIT_OVERRIDE_DYNAREC]  = (uint64_t) (uintptr_t) &cpu_override_dynarec;
    lit[LINK_LIT_NMI]               = (uint64_t) (uintptr_t) &nmi;
    lit[LINK_LIT_PIC]               = (uint64_t) (uintptr_t) &pic;
    lit[LINK_LIT_CUR_STATUS]        = (uint64_t) (uintptr_t) &cpu_cur_status;
    lit[LINK_LIT_TSC]               = (uint64_t) (uintptr_t) &tsc;
    lit[LINK_LIT_TSC_OLD]           = (uint64_t) (uintptr_t) &tsc_old;
    lit[LINK_LIT_CYCLES_OLD]        = (uint64_t) (uintptr_t) &cycles_old;
    lit[LINK_LIT_TIMER_TARGET]      = (uint64_t) (uintptr_t) &timer_target;
    lit[LINK_LIT_GET_PHYS_VIRT]     = (uint64_t) (uintptr_t) &get_phys_virt;
    lit[LINK_LIT_GET_PHYS_PHYS]     = (uint64_t) (uintptr_t) &get_phys_phys;
    lit[LINK_LIT_OLDCS]             = (uint64_t) (uintptr_t) &oldcs;
    lit[LINK_LIT_OLDCPL]            = (uint64_t) (uintptr_t) &oldcpl;
    lit[LINK_LIT_FETCH_DEVICE]      = (uint64_t) (uintptr_t) &cpu_fetch_device;
    lit[LINK_LIT_NMI_ENABLE]        = (uint64_t) (uintptr_t) &nmi_enable;
    lit[LINK_LIT_NMI_MASK]          = (uint64_t) (uintptr_t) &nmi_mask;
    lit[LINK_LIT_RAMMASK]           = (uint64_t) (uintptr_t) &rammask;
    lit[LINK_LIT_READLOOKUP2]       = (uint64_t) (uintptr_t) readlookup2;
    lit[LINK_LIT_RAM]               = (uint64_t) (uintptr_t) &ram;
    lit[LINK_LIT_DR_ARMED]          = (uint64_t) (uintptr_t) &cpu_dr_armed;
    lit[LINK_LIT_IOAPIC_PATH]       = (uint64_t) (uintptr_t) &ioapic_irq_path_active;
    lit[LINK_LIT_LAPIC_EXTINT]      = (uint64_t) (uintptr_t) &cpu_lapic_extint_live[0];
    lit[LINK_LIT_LAPIC_IRR]         = (uint64_t) (uintptr_t) &cpu_lapic_irr_live;

    codegen_link_rout = &block_write_data[block_pos];

    /*X17 = the literal pool. X1 = the exit's slot, whose exit_pc and exit_cs
      must match cpu_state.pc and the CS base and whose entry must be set;
      X15 = its entry; W2 = cs + pc*/
    host_arm64_ADR(block, REG_X17, (int) ((uintptr_t) lit - (uintptr_t) &block_write_data[block_pos]));
    host_arm64_LDR_IMM_W(block, REG_W2, REG_CPUSTATE, rep_state_offset(&cpu_state.pc));
    host_arm64_LDR_IMM_W(block, REG_W3, REG_CPUSTATE, rep_state_offset(&cpu_state.seg_cs.base));
    host_arm64_LDP_IMM_X(block, REG_X5, REG_X15, REG_X0, LINK_SLOT(exit_pc));
    host_arm64_ORRX_REG(block, REG_X4, REG_X2, REG_X3, 32);
    host_arm64_ADD_REG(block, REG_W2, REG_W2, REG_W3, 0);
    host_arm64_MOVX_REG(block, REG_X1, REG_X0, 0);
    host_arm64_CMPX_REG(block, REG_X5, REG_X4);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    miss[nr_miss++] = host_arm64_CBZ_SHORT_(block, REG_X15);

    /*Target not freed or changed since the slot was filled. W6 = the slot's
      status | npxc << 16*/
    host_arm64_LDR_IMM_X(block, REG_X4, REG_X1, LINK_SLOT(block));
    host_arm64_LDP_IMM_W(block, REG_W5, REG_W6, REG_X1, LINK_SLOT(gen));
    host_arm64_LDR_IMM_W(block, REG_W4, REG_X4, offsetof(codeblock_t, link_gen));
    host_arm64_CMP_REG(block, REG_W4, REG_W5);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);

    /*Nothing pending for the loop, and the loop would pick the recompiler.
      An armed debug breakpoint is a miss, so the loop makes its per-block
      check (dr_blocks_cache()). W7 = abrt and smi_line in its top half,
      W14 = cycles, X12 = &nmi*/
    host_arm64_LDP_IMM_W(block, REG_W7, REG_W14, REG_CPUSTATE, rep_state_offset(&cpu_state.abrt) - 2);
    host_arm64_LDR_IMM_X(block, REG_X13, REG_X17, LINK_LIT(LINK_LIT_DR_ARMED));
    host_arm64_LDP_IMM_X(block, REG_X3, REG_X4, REG_X17, LINK_LIT(LINK_LIT_WAS_RESET));
    host_arm64_LDP_IMM_X(block, REG_X5, REG_X8, REG_X17, LINK_LIT(LINK_LIT_CPU_INIT));
    /*X9 = &nmi_enable, loaded only as the pair of the next entry*/
    host_arm64_LDP_IMM_X(block, REG_X9, REG_X10, REG_X17, LINK_LIT(LINK_LIT_NMI_ENABLE));
    host_arm64_LDP_IMM_X(block, REG_X11, REG_X12, REG_X17, LINK_LIT(LINK_LIT_OVERRIDE_DYNAREC));
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X3, 0);
    host_arm64_LDR_IMM_W(block, REG_W4, REG_X4, 0);
    host_arm64_LDR_IMM_W(block, REG_W5, REG_X5, 0);
    host_arm64_LDR_IMM_W(block, REG_W8, REG_X8, 0);
    host_arm64_LDR_IMM_W(block, REG_W10, REG_X10, 0);
    host_arm64_LDR_IMM_W(block, REG_W11, REG_X11, 0);
    host_arm64_LDR_IMM_W(block, REG_W13, REG_X13, 0);
    host_arm64_AND_IMM(block, REG_W7, REG_W7, 0xffff0000);
    host_arm64_ORR_REG(block, REG_W3, REG_W3, REG_W4, 0);
    host_arm64_ORR_REG(block, REG_W5, REG_W5, REG_W8, 0);
    host_arm64_ORR_REG(block, REG_W7, REG_W7, REG_W11, 0);
    host_arm64_ORR_REG(block, REG_W3, REG_W3, REG_W5, 0);
    host_arm64_ORR_REG(block, REG_W7, REG_W7, REG_W10, 0);
    host_arm64_ORR_REG(block, REG_W5, REG_W5, REG_W13, 0);
    host_arm64_ORR_REG(block, REG_W3, REG_W3, REG_W7, 0);
    miss[nr_miss++] = host_arm64_CBNZ_SHORT_(block, REG_W3);

    /*NMI_TAKEABLE(): nmi && nmi_enable && (nmi_mask || (nmi & 2)); the
      rest is out of line, with W12 = nmi*/
    host_arm64_LDR_IMM_W(block, REG_W12, REG_X12, 0);
    nmi_set  = host_arm64_CBNZ_SHORT_(block, REG_W12);
    nmi_done = &block_write_data[block_pos];

    /*Trap flag, and an interrupt the loop would take. X13 = CR0 | flags << 32*/
    host_arm64_LDR_IMM_X(block, REG_X13, REG_CPUSTATE, rep_state_offset(&cpu_state.CR0.l));
    host_arm64_LDP_IMM_X(block, REG_X3, REG_X4, REG_X17, LINK_LIT(LINK_LIT_PIC));
    miss[nr_miss++] = host_arm64_TBNZ_SHORT_(block, REG_X13, 32 + 8);
    irq_ok          = host_arm64_TBZ_SHORT_(block, REG_X13, 32 + 9);
    host_arm64_LDRB_IMM_W(block, REG_W5, REG_X3, offsetof(pic_t, int_pending));
    miss[nr_miss++] = host_arm64_CBNZ_SHORT_(block, REG_W5);
    /*With the IOAPIC routing interrupts, x86_external_interrupt_pending()
      also looks at local APIC 0: an ExtINT or anything in its IRR is a
      miss, and the C loop works out whether it is deliverable*/
    host_arm64_LDR_IMM_X(block, REG_X5, REG_X17, LINK_LIT(LINK_LIT_IOAPIC_PATH));
    host_arm64_LDRB_IMM_W(block, REG_W5, REG_X5, 0);
    irq_ok_2 = host_arm64_CBZ_SHORT_(block, REG_W5);
    host_arm64_LDR_IMM_X(block, REG_X5, REG_X17, LINK_LIT(LINK_LIT_LAPIC_EXTINT));
    host_arm64_LDRB_IMM_W(block, REG_W5, REG_X5, 0);
    miss[nr_miss++] = host_arm64_CBNZ_SHORT_(block, REG_W5);
    host_arm64_LDR_IMM_X(block, REG_X5, REG_X17, LINK_LIT(LINK_LIT_LAPIC_IRR));
    host_arm64_LDR_IMM_W(block, REG_W5, REG_X5, 0);
    miss[nr_miss++] = host_arm64_TBNZ_SHORT_(block, REG_W5, 0);
    host_arm64_short_branch_set_offset(irq_ok, &block_write_data[block_pos]);
    host_arm64_short_branch_set_offset(irq_ok_2, &block_write_data[block_pos]);

    /*Status and FPU rounding as when the slot was filled, compared as one
      word against W6. Only the C loop refreshes the PC24 and RC_NZ status
      bits from npxc, so they can be stale after an FLDCW in a linked block;
      the npxc compare decides them instead. Everything else that reads
      those two bits runs after that refresh. The C loop also sets NOTFLATES
      from ES before each lookup, and not every ES load keeps it current, so
      it is worked out again here from ES itself: W8 = 0 if ES is flat.*/
    host_arm64_LDRH_IMM(block, REG_W4, REG_X4, 0);
    host_arm64_AND_IMM(block, REG_W4, REG_W4, ~(uint32_t) CPU_STATUS_NOTFLATES);
    host_arm64_LDR_IMM_W(block, REG_W8, REG_CPUSTATE, rep_state_offset(&cpu_state.seg_es.base));
    host_arm64_LDR_IMM_W(block, REG_W9, REG_CPUSTATE, rep_state_offset(&cpu_state.seg_es.limit_low));
    host_arm64_LDR_IMM_W(block, REG_W10, REG_CPUSTATE, rep_state_offset(&cpu_state.seg_es.limit_high));
    host_arm64_ADD_IMM(block, REG_W10, REG_W10, 1);
    host_arm64_ORR_REG(block, REG_W8, REG_W8, REG_W9, 0);
    host_arm64_ORR_REG(block, REG_W8, REG_W8, REG_W10, 0);
    es_flat = host_arm64_CBZ_SHORT_(block, REG_W8);
    host_arm64_ORR_IMM(block, REG_W4, REG_W4, CPU_STATUS_NOTFLATES);
    host_arm64_short_branch_set_offset(es_flat, &block_write_data[block_pos]);
    host_arm64_LDRH_IMM(block, REG_W5, REG_CPUSTATE, rep_state_offset(&cpu_state.npxc));
    host_arm64_AND_IMM(block, REG_W5, REG_W5, 0x0f00);
    host_arm64_ORR_REG(block, REG_W4, REG_W4, REG_W5, 16);
    host_arm64_EOR_REG(block, REG_W4, REG_W4, REG_W6, 0);
    host_arm64_TST_IMM(block, REG_W4, ~(uint32_t) (CPU_STATUS_FPU_PC24 | CPU_STATUS_FPU_RC_NZ));
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);

    /*The TSC. If it moved, update_tsc() ran inside the block and the C loop
      does the rest of that arithmetic. X3 = &tsc, X11 = new tsc,
      X4 = &tsc_old, X5 = &cycles_old*/
    host_arm64_LDP_IMM_X(block, REG_X3, REG_X4, REG_X17, LINK_LIT(LINK_LIT_TSC));
    host_arm64_LDP_IMM_X(block, REG_X5, REG_X6, REG_X17, LINK_LIT(LINK_LIT_CYCLES_OLD));
    host_arm64_LDR_IMM_X(block, REG_X11, REG_X3, 0);
    host_arm64_LDR_IMM_X(block, REG_X7, REG_X4, 0);
    host_arm64_CMPX_REG(block, REG_X11, REG_X7);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_LDR_IMM_W(block, REG_W7, REG_X5, 0);
    host_arm64_SUB_REG(block, REG_W7, REG_W7, REG_W14, 0);
    host_arm64_CMP_IMM(block, REG_W7, 0);
    miss[nr_miss++] = host_arm64_BLE_SHORT_(block);
    host_arm64_ADDX_REG_UXTW(block, REG_X11, REG_X11, REG_W7);
    /*No timer due at the new TSC*/
    host_arm64_LDR_IMM_X(block, REG_X7, REG_X6, 0);
    host_arm64_SUBX_REG(block, REG_X7, REG_X7, REG_X11);
    host_arm64_CMPX_IMM(block, REG_X7, 0);
    miss[nr_miss++] = host_arm64_BLE_SHORT_(block);
    /*Slice not used up*/
    host_arm64_CMP_IMM(block, REG_W14, 0);
    miss[nr_miss++] = host_arm64_BLE_SHORT_(block);

    /*Paging and cache-disable bits as when the slot was filled. W9 = them,
      W12 = phys_page (the upper halves hold virt and rammask)*/
    host_arm64_LDP_IMM_X(block, REG_X9, REG_X12, REG_X1, LINK_SLOT(cr0_bits));
    host_arm64_AND_IMM(block, REG_W10, REG_W13, 0xc0000000);
    host_arm64_CMP_REG(block, REG_W10, REG_W9);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);

    /*get_phys(cs + pc) gives phys_page. X6 = &get_phys_virt,
      X7 = &get_phys_phys, W8 = nonzero if get_phys() would miss its cache*/
    host_arm64_LDP_IMM_X(block, REG_X6, REG_X7, REG_X17, LINK_LIT(LINK_LIT_GET_PHYS_VIRT));
    host_arm64_LDR_IMM_W(block, REG_W8, REG_X6, 0);
    host_arm64_EOR_REG(block, REG_W8, REG_W8, REG_W2, 0);
    host_arm64_ANDS_IMM(block, REG_W8, REG_W8, 0xfffff000);
    uncached = host_arm64_BNE_SHORT_(block);
    host_arm64_LDR_IMM_W(block, REG_W10, REG_X7, 0);
    host_arm64_CMP_REG(block, REG_W10, REG_W12);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    xlat_done = &block_write_data[block_pos];
    host_arm64_LDP_IMM_X(block, REG_X10, REG_X13, REG_X1, LINK_SLOT(mem_exec));
    host_arm64_LDR_IMM_X(block, REG_X10, REG_X10, 0);
    miss[nr_miss++] = host_arm64_CBZ_SHORT_(block, REG_X10);

    /*The block lookup and the checks made before running it. X9 =
      block_nr | top_mask << 16 | top << 24 | two_page << 32*/
    host_arm64_LDR_IMM_X(block, REG_X9, REG_X1, LINK_SLOT(block_nr));
    host_arm64_LDRH_IMM(block, REG_W10, REG_X13, 0);
    host_arm64_EOR_REG(block, REG_W10, REG_W10, REG_W9, 0);
    host_arm64_TST_IMM(block, REG_W10, 0xffff);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_LDP_IMM_X(block, REG_X10, REG_X13, REG_X1, LINK_SLOT(page_mask));
    host_arm64_LDR_IMM_X(block, REG_X13, REG_X13, 0);
    host_arm64_TSTX_REG(block, REG_X13, REG_X10);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_LDR_IMM_W(block, REG_W10, REG_CPUSTATE, rep_state_offset(&cpu_state.TOP));
    host_arm64_UBFX(block, REG_W13, REG_W9, 16, 8);
    host_arm64_AND_REG(block, REG_W10, REG_W10, REG_W13, 0);
    host_arm64_UBFX(block, REG_W13, REG_W9, 24, 8);
    host_arm64_CMP_REG(block, REG_W10, REG_W13);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    two_page      = host_arm64_TBNZ_SHORT_(block, REG_X9, 32);
    two_page_done = &block_write_data[block_pos];

    /*Every check passed: the C loop's stores, then the next block. W10 =
      CS access | selector << 16*/
    host_arm64_STR_IMM_Q(block, REG_X11, REG_X3, 0);
    host_arm64_STR_IMM_Q(block, REG_X11, REG_X4, 0);
    host_arm64_STR_IMM_W(block, REG_W14, REG_X5, 0);
    host_arm64_LDR_IMM_W(block, REG_W10, REG_CPUSTATE, rep_state_offset(&cpu_state.seg_cs.access));
    host_arm64_LDP_IMM_X(block, REG_X3, REG_X4, REG_X17, LINK_LIT(LINK_LIT_OLDCS));
    host_arm64_UBFX(block, REG_W13, REG_W10, 16, 16);
    host_arm64_STRH_IMM(block, REG_W13, REG_X3, 0);
    host_arm64_UBFX(block, REG_W10, REG_W10, 5, 2);
    host_arm64_STR_IMM_W(block, REG_W10, REG_X4, 0);
    /*get_phys() only writes its cache when it misses*/
    cached = host_arm64_CBZ_SHORT_(block, REG_W8);
    host_arm64_STR_IMM_W(block, REG_W2, REG_X6, 0);
    host_arm64_STR_IMM_W(block, REG_W12, REG_X7, 0);
    host_arm64_short_branch_set_offset(cached, &block_write_data[block_pos]);
    /*cpu_fetch_device and cpu_halted, cleared as the C loop does*/
    host_arm64_LDP_IMM_X(block, REG_X3, REG_X4, REG_X17, LINK_LIT(LINK_LIT_FETCH_DEVICE));
    host_arm64_STR_IMM_W(block, REG_WZR, REG_X3, 0);
    host_arm64_STR_IMM_W(block, REG_WZR, REG_X4, 0);
    /*The block is about to run: the eviction sweep passes it over once*/
    host_arm64_LDR_IMM_X(block, REG_X3, REG_X1, LINK_SLOT(block));
    host_arm64_MOVZ_IMM(block, REG_W4, 1);
    host_arm64_STRB_IMM(block, REG_W4, REG_X3, offsetof(codeblock_t, used));
    host_arm64_BR(block, REG_X15);

    /*Miss: the block's own epilogue, X0 still the slots. codegen_exit_rout
      past its X0 = 0 is that epilogue.*/
    miss_dest = &block_write_data[block_pos];
    host_arm64_B(block, codegen_link_miss_rout);

    /*nmi set: miss only if nmi_enable is too, and nmi_mask or an NMI from
      the local APIC (nmi bit 1, which port 70h does not gate)*/
    host_arm64_short_branch_set_offset(nmi_set, &block_write_data[block_pos]);
    host_arm64_LDR_IMM_X(block, REG_X3, REG_X17, LINK_LIT(LINK_LIT_NMI_ENABLE));
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X3, 0);
    nmi_ok = host_arm64_CBZ_SHORT_(block, REG_W3);
    host_arm64_short_branch_set_offset(nmi_ok, nmi_done);
    host_arm64_LDR_IMM_X(block, REG_X3, REG_X17, LINK_LIT(LINK_LIT_NMI_MASK));
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X3, 0);
    miss[nr_miss++] = host_arm64_CBNZ_SHORT_(block, REG_W3);
    miss[nr_miss++] = host_arm64_TBNZ_SHORT_(block, REG_W12, 1);
    host_arm64_B(block, nmi_done);

    /*get_phys() cache miss: it would store the full address in
      get_phys_virt, and translate through readlookup2[] with paging on or
      through rammask with paging off*/
    host_arm64_short_branch_set_offset(uncached, &block_write_data[block_pos]);
    no_paging = host_arm64_TBZ_SHORT_(block, REG_W9, 31);
    host_arm64_LDP_IMM_X(block, REG_X10, REG_X13, REG_X1, LINK_SLOT(xlat));
    host_arm64_LDR_IMM_X(block, REG_X10, REG_X10, 0);
    host_arm64_CMPX_REG(block, REG_X10, REG_X13);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_B(block, xlat_done);
    host_arm64_short_branch_set_offset(no_paging, &block_write_data[block_pos]);
    host_arm64_LDR_IMM_X(block, REG_X10, REG_X17, LINK_LIT(LINK_LIT_RAMMASK));
    host_arm64_LDR_IMM_W(block, REG_W10, REG_X10, 0);
    host_arm64_LDR_IMM_W(block, REG_W13, REG_X1, LINK_SLOT(rammask));
    host_arm64_CMP_REG(block, REG_W10, REG_W13);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_B(block, xlat_done);

    /*Target spans two pages: the second page's dirty mask, and its page from
      get_phys_noabrt() at the address the C loop takes from the block's pc,
      against phys_2. The block's own fields are read as the C loop reads
      them. A readlookup2[] miss would make get_phys_noabrt() walk the page
      tables, so it is a miss here. X9 = the block, W10 = the address,
      W13 = its physical address*/
    host_arm64_short_branch_set_offset(two_page, &block_write_data[block_pos]);
    host_arm64_LDR_IMM_X(block, REG_X9, REG_X1, LINK_SLOT(block));
    host_arm64_LDR_IMM_X(block, REG_X10, REG_X9, offsetof(codeblock_t, page_mask2));
    one_page = host_arm64_CBZ_SHORT_(block, REG_X10);
    host_arm64_short_branch_set_offset(one_page, two_page_done);
    host_arm64_LDR_IMM_X(block, REG_X13, REG_X9, offsetof(codeblock_t, dirty_mask2));
    host_arm64_LDR_IMM_X(block, REG_X13, REG_X13, 0);
    host_arm64_TSTX_REG(block, REG_X13, REG_X10);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_LDR_IMM_W(block, REG_W10, REG_X9, offsetof(codeblock_t, pc));
    host_arm64_LDRH_IMM(block, REG_W13, REG_X9, offsetof(codeblock_t, flags));
    host_arm64_ADD_IMM(block, REG_W10, REG_W10, 0x400);
    byte_mask = host_arm64_TBZ_SHORT_(block, REG_W13, 5);
    host_arm64_SUB_IMM(block, REG_W10, REG_W10, 0x400 - 0x40);
    host_arm64_short_branch_set_offset(byte_mask, &block_write_data[block_pos]);
    host_arm64_LDR_IMM_W(block, REG_W13, REG_X1, LINK_SLOT(cr0_bits));
    no_paging_2 = host_arm64_TBZ_SHORT_(block, REG_W13, 31);
    host_arm64_LDR_IMM_X(block, REG_X13, REG_X17, LINK_LIT(LINK_LIT_READLOOKUP2));
    host_arm64_UBFX(block, REG_W16, REG_W10, 12, 20);
    host_arm64_LDRX_REG_LSL3(block, REG_X13, REG_X13, REG_X16);
    host_arm64_CMN_IMM(block, REG_W13, 1);
    miss[nr_miss++] = host_arm64_BEQ_SHORT_(block);
    host_arm64_LDR_IMM_X(block, REG_X16, REG_X17, LINK_LIT(LINK_LIT_RAM));
    host_arm64_LDR_IMM_X(block, REG_X16, REG_X16, 0);
    host_arm64_ADDX_REG_UXTW(block, REG_X13, REG_X13, REG_W10);
    host_arm64_SUBX_REG(block, REG_X13, REG_X13, REG_X16);
    xlat_2_done = &block_write_data[block_pos];
    host_arm64_LDR_IMM_W(block, REG_W16, REG_X9, offsetof(codeblock_t, phys_2));
    host_arm64_EOR_REG(block, REG_W16, REG_W16, REG_W13, 0);
    host_arm64_TST_IMM(block, REG_W16, 0xfffff000);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_B(block, two_page_done);
    host_arm64_short_branch_set_offset(no_paging_2, &block_write_data[block_pos]);
    host_arm64_LDR_IMM_X(block, REG_X16, REG_X17, LINK_LIT(LINK_LIT_RAMMASK));
    host_arm64_LDR_IMM_W(block, REG_W16, REG_X16, 0);
    host_arm64_AND_REG(block, REG_W13, REG_W10, REG_W16, 0);
    host_arm64_B(block, xlat_2_done);

    if (nr_miss > LINK_MAX_MISS)
        fatal("build_link_routine - too many miss branches\n");
    if (block_write_data != start_data)
        fatal("build_link_routine - routine does not fit in one allocator block\n");
    for (int c = 0; c < nr_miss; c++)
        host_arm64_short_branch_set_offset(miss[c], miss_dest);
}
#    endif

void
codegen_backend_init(void)
{
    codeblock_t *block;
    uint8_t      large_block = 0;
    uint8_t      large_hash = 0;

    codeblock      = plat_mmap(BLOCK_SIZE * sizeof(codeblock_t), 0, &large_block);
    codeblock_hash = plat_mmap(HASH_SIZE * sizeof(codeblock_t *), 0, &large_hash);

    if (large_block)
        pclog("Allocated %" PRIu64 " bytes of large pages for codeblock pointers\n", (uint64_t) (BLOCK_SIZE * sizeof(codeblock_t)));
    if (large_hash)
        pclog("Allocated %" PRIu64 " bytes of large pages for codeblock hashes\n", (uint64_t) (HASH_SIZE * sizeof(codeblock_t *)));

    for (int c = 0; c < BLOCK_SIZE; c++) {
        codeblock[c].valid = 0;
    }

    block_current         = 0;
    block_pos             = 0;
    block                 = &codeblock[block_current];
    block->head_mem_block = codegen_allocator_allocate(NULL, block_current);
    block->data           = codeblock_allocator_get_ptr(block->head_mem_block);
    block_write_data      = block->data;
    build_loadstore_routines(block);

    codegen_fp_round = &block_write_data[block_pos];
    build_fp_round_routine(block, 0);
    codegen_fp_round_quad = &block_write_data[block_pos];
    build_fp_round_routine(block, 1);

    codegen_alloc(block, 128);
    codegen_gpf_rout = &block_write_data[block_pos];
    host_arm64_mov_imm(block, REG_ARG0, 0);
    host_arm64_mov_imm(block, REG_ARG1, 0);
    host_arm64_call(block, (void *) x86gpf);

    codegen_exit_rout = &block_write_data[block_pos];
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    /*No slots: a block leaving from here is never linked*/
    host_arm64_MOVZ_IMM(block, REG_W0, 0);
    codegen_link_miss_rout = &block_write_data[block_pos];
#    endif
    host_arm64_LDP_POSTIDX_X(block, REG_X19, REG_X20, REG_XSP, 64);
    host_arm64_LDP_POSTIDX_X(block, REG_X21, REG_X22, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X23, REG_X24, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X25, REG_X26, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X27, REG_X28, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);

    /*#SS(0): an operand through SS outside its limit (CHECK_SEG_LIMITS)*/
    codegen_alloc(block, 40);
    codegen_ss_rout = &block_write_data[block_pos];
    host_arm64_mov_imm(block, REG_ARG0, 0);
    host_arm64_mov_imm(block, REG_ARG1, 0);
    host_arm64_call(block, (void *) x86ss);
    host_arm64_B(block, codegen_exit_rout);

    for (int is_stos = 0; is_stos < 2; is_stos++) {
        for (int shift = 0; shift < 3; shift++) {
            for (int a32 = 0; a32 < 2; a32++) {
                codegen_rep_rout[is_stos][shift][a32] = &block_write_data[block_pos];
                build_rep_routine(block, is_stos, shift, a32, codegen_rep_elem[is_stos][shift][a32]);
            }
        }
    }

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    build_link_routine(block);
#    endif

    block_write_data = NULL;

    codegen_allocator_clean_blocks(block->head_mem_block);

    {
        const void *veneer_targets[] = { codegen_exit_rout, codegen_gpf_rout, codegen_ss_rout };

        codegen_allocator_set_veneers(veneer_targets, sizeof(veneer_targets) / sizeof(veneer_targets[0]));
    }

    asm("mrs %0, fpcr\n"
        : "=r"(cpu_state.old_fp_control));
}

void
codegen_set_rounding_mode(int mode)
{
    if (mode < 0 || mode > 3)
        fatal("codegen_set_rounding_mode - invalid mode\n");
    cpu_state.new_fp_control = mode << 3;
}

/*R10 - cpu_state*/
void
codegen_backend_prologue(codeblock_t *block)
{
    block_pos = BLOCK_START;

    /*Entry code*/

    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X27, REG_X28, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X25, REG_X26, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X23, REG_X24, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X21, REG_X22, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X19, REG_X20, REG_XSP, -64);

    host_arm64_MOVX_IMM(block, REG_CPUSTATE, (uint64_t) &cpu_state);

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    link_site = 0;
#    endif

    if (block->flags & CODEBLOCK_HAS_FPU) {
        host_arm64_LDR_IMM_W(block, REG_TEMP, REG_CPUSTATE, (uintptr_t) &cpu_state.TOP - (uintptr_t) &cpu_state);
        host_arm64_SUB_IMM(block, REG_TEMP, REG_TEMP, block->TOP);
        host_arm64_STR_IMM_W(block, REG_TEMP, REG_XSP, IREG_TOP_diff_stack_offset);
    }
}

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
/*Leave the block with every register written back and cpu_state.pc set, as
  at its end. The link routine returns through the same register restores on
  a miss. Each exit has a slot of its own, so the block's end and its taken
  branches do not take turns filling the same one; a block with more exits
  than slots shares them from the first again.*/
void
codegen_link_exit(codeblock_t *block)
{
    codegen_link_slot_t *slot = &codegen_link_slots[get_block_nr(block)][link_site++ % CODEGEN_LINK_SLOTS];

    codegen_alloc(block, 20);
    host_arm64_MOVX_IMM(block, REG_X0, (uint64_t) (uintptr_t) slot);
    host_arm64_B(block, codegen_link_rout);
}
#    endif

void
codegen_backend_epilogue(codeblock_t *block)
{
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    if (codegen_block_link) {
        codegen_link_exit(block);
        codegen_allocator_clean_blocks(block->head_mem_block);
        return;
    }
#    endif
    host_arm64_LDP_POSTIDX_X(block, REG_X19, REG_X20, REG_XSP, 64);
    host_arm64_LDP_POSTIDX_X(block, REG_X21, REG_X22, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X23, REG_X24, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X25, REG_X26, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X27, REG_X28, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);

    codegen_allocator_clean_blocks(block->head_mem_block);
}

#endif
