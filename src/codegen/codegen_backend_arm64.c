#if defined __aarch64__ || defined _M_ARM64

#    include <inttypes.h>
#    include <stddef.h>
#    include <stdlib.h>
#    include <stdint.h>
#    if defined(__APPLE__) && defined(__aarch64__)
#        include <pthread.h>
#    endif
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

extern uint32_t x87_dyn_ld80(uint32_t seg_nr);
extern uint32_t x87_dyn_st80(uint32_t seg_nr);
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
void *codegen_mem_store_slow_byte;
void *codegen_mem_store_slow_word;
void *codegen_mem_store_slow_long;

void *codegen_fp_round;
void *codegen_fp_round_quad;

void *codegen_gpf_rout;
void *codegen_ss_rout;
void *codegen_exit_rout;
void *(*codegen_enter_block)(void *code);
void *codegen_rep_rout[2][3][2];

/*Entries to the exit routines that take 1..n cycles off cycles first, one
  per routine and count, so an exit that charges them keeps its single
  branch and changes only its target (codegen_exit_dest). Nearly every
  exit's charge is the instruction's own cycles and those of the few before
  it; a larger one gets an arm in the block. Each is a veneer target too.*/
#    define EXIT_CHARGED_EXIT 128
#    define EXIT_CHARGED_GPF  64
#    define EXIT_CHARGED_SS   32
static void *codegen_exit_charged[EXIT_CHARGED_EXIT];
static void *codegen_gpf_charged[EXIT_CHARGED_GPF];
static void *codegen_ss_charged[EXIT_CHARGED_SS];

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
codegen_link_slot_t codegen_link_slots[BLOCK_SIZE][CODEGEN_LINK_SLOTS];
#    endif
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
static void *codegen_link_rout;
/*The block epilogue that returns X0 as it is*/
static void *codegen_link_miss_rout;
/*Exits emitted so far for the block being compiled; each gets its own slot*/
static int link_site;
/*The routine's branch to the set lookup, and where that comes back to*/
static uint32_t *link_set_branch;
/*The link routine's branch to its prefetch of the next block's code*/
static uint32_t *link_next_branch;
/*The link routine's branch to its short way, and where that comes back*/
static uint32_t *link_fast_branch;
static uint8_t  *link_fast_normal;
static uint8_t  *link_set_return;
/*The link routine's entry that compares only the pc (codegen_link_exit())*/
static uint8_t  *codegen_link_rout_direct;
/*This block's exits to it, sent to the routine's start instead where the
  block turns out to share slots between exits or to call C*/
#    define LINK_DIRECT_SITES_MAX 16
static uint32_t *link_direct_sites[LINK_DIRECT_SITES_MAX];
static int       nr_link_direct_sites;
/*The link routine's branch to the static TOP check and where it goes on,
  and its miss: the check is out of line in the set routine's block*/
static uint32_t *link_top_check;
static uint8_t  *link_top_done;
static uint8_t  *link_miss_dest;
static uint32_t *link_nmi_set;
static uint8_t  *link_nmi_done;
static uint32_t *link_apic_set;
static uint8_t  *link_irq_done;
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
#    if CODEGEN_BACKEND_PINNED_CYCLES != 27
    { REG_X27, 0}
#    endif
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

/*A misaligned load that stays inside its page and has a read lookup: what
  readmemwl(), readmemll() and readmemql() do on their in-page branch, the
  timing_misaligned charge under the same Cyrix alignment rule and the load
  through the lookup. A page crossing or a missing lookup branches to the C
  path through to_c[0] and to_c[1].
  In - W0 = address, X1 = readlookup2[] entry*/
static void
build_load_misaligned(codeblock_t *block, int size, int is_float, uint32_t **to_c)
{
    uint32_t *skip   = NULL;
    uint32_t *charge = NULL;

    /*The last byte is in the next page when bit 12 differs*/
    host_arm64_ADD_IMM(block, REG_W2, REG_W0, size - 1);
    host_arm64_EOR_REG(block, REG_W2, REG_W2, REG_W0, 0);
    to_c[0] = host_arm64_TBNZ_SHORT_(block, REG_W2, 12);
    host_arm64_CMPX_IMM(block, REG_X1, -1);
    to_c[1] = host_arm64_BEQ_SHORT_(block);

    /*readmem*l() clears high_page on every call; checkio()'s I/O map read
      through readmemw_n() does not and would see a stale one*/
    host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) &high_page);
    host_arm64_STRB_IMM(block, REG_WZR, REG_X2, 0);

    if (size != 8) {
        host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) &cpu_cyrix_alignment);
        host_arm64_LDR_IMM_W(block, REG_W2, REG_X2, 0);
        charge = host_arm64_CBZ_SHORT_(block, REG_W2);
        host_arm64_AND_IMM(block, REG_W2, REG_W0, 7);
        if (size == 2) {
            host_arm64_CMP_IMM(block, REG_W2, 7);
            skip = host_arm64_BNE_SHORT_(block);
        } else {
            host_arm64_CMP_IMM(block, REG_W2, 4);
            skip = host_arm64_BLE_SHORT_(block);
        }
        host_arm64_short_branch_set_offset(charge, &block_write_data[block_pos]);
    }
    host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) &timing_misaligned);
    host_arm64_LDR_IMM_W(block, REG_W2, REG_X2, 0);
    codegen_direct_read_32(block, REG_W3, &cpu_state._cycles);
    host_arm64_SUB_REG(block, REG_W3, REG_W3, REG_W2, 0);
    codegen_direct_write_32(block, &cpu_state._cycles, REG_W3);
    if (skip)
        host_arm64_short_branch_set_offset(skip, &block_write_data[block_pos]);

    if (size == 2)
        host_arm64_LDRH_REG(block, REG_W0, REG_W1, REG_W0);
    else if (size == 4 && !is_float)
        host_arm64_LDR_REG(block, REG_W0, REG_W1, REG_W0);
    else if (size == 4 && is_float)
        host_arm64_LDR_REG_F32(block, REG_V_TEMP, REG_W1, REG_W0);
    else
        host_arm64_LDR_REG_F64(block, REG_V_TEMP, REG_W1, REG_W0);
    host_arm64_MOVZ_IMM(block, REG_W1, 0);
    host_arm64_RET(block, REG_X30);
}

/*ea_rule: take the direct load whenever the address is at most 0xffc into
  its page, aligned or not, as the interpreter's eal_r path does; readmemwl
  and readmemll charge timing_misaligned only on the slow path.*/
static void
build_load_routine(codeblock_t *block, int size, int is_float, int ea_rule)
{
    uint32_t *branch_offset;
    uint32_t *misaligned_offset = NULL;
    uint32_t *to_c[2];
    int       has_misaligned = !ea_rule && (size != 1);

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
    /*All of it in one allocator block, so the branches to the slow path are
      single B.conds: with the misaligned path at most 55 instructions with
      every MOVX_IMM at four and the C call through MOVX_IMM + BLR*/
    codegen_alloc(block, has_misaligned ? 220 : 160);
    host_arm64_MOV_REG_LSR(block, REG_W1, REG_W0, 12);
    host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) readlookup2);
    host_arm64_LDRX_REG_LSL3(block, REG_X1, REG_X2, REG_X1);
    if (ea_rule) {
        host_arm64_AND_IMM(block, REG_W2, REG_W0, 0xfff);
        host_arm64_CMP_IMM(block, REG_W2, 0xffc);
        misaligned_offset = host_arm64_BHI_SHORT_(block);
    } else if (size != 1) {
        host_arm64_TST_IMM(block, REG_W0, size - 1);
        misaligned_offset = host_arm64_BNE_SHORT_(block);
    }
    host_arm64_CMPX_IMM(block, REG_X1, -1);
    branch_offset = host_arm64_BEQ_SHORT_(block);
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

    if (has_misaligned) {
        host_arm64_short_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);
        build_load_misaligned(block, size, is_float, to_c);
        host_arm64_short_branch_set_offset(to_c[0], &block_write_data[block_pos]);
        host_arm64_short_branch_set_offset(to_c[1], &block_write_data[block_pos]);
    }
    host_arm64_short_branch_set_offset(branch_offset, &block_write_data[block_pos]);
    if (ea_rule)
        host_arm64_short_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);
    /*C may run a device's handler: the link routine checks again (cpu_state.link_c_ran)*/
    host_arm64_MOVZ_IMM(block, REG_W2, 1);
    host_arm64_STRB_IMM(block, REG_W2, REG_CPUSTATE, (uintptr_t) &cpu_state.link_c_ran - (uintptr_t) &cpu_state);
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
/*An aligned byte, word or dword store to a RAM page whose writes are tracked
  (page_lookup): what writemem*l() and mem_write_ram*_page() do, without the
  two calls. The store never reaches the next 64-byte chunk, so only one dirty
  bit and one byte-mask word change. writemem*l()'s addr64 and addr64a[] are
  left alone: every user fills them before reading them, as are
  mem_logical_addr and high_page. Anything else returns
  through to_c[] to the C path.
  In - W0 = address, W1 = data. Returns the number of to_c[] branches.*/
static int
build_store_tracked(codeblock_t *block, int size, uint32_t **to_c)
{
    uint32_t *done[4];
    uint32_t *add_evict;
    uint32_t *not_known;
    uint32_t *known_store;
    uint32_t *no_code;
    int       nr_to_c   = 0;
    int       byte_mask = (1 << size) - 1;
    void     *write_fn;
    int       write_offset;

    if (size == 1) {
        write_fn     = (void *) mem_write_ramb_page;
        write_offset = offsetof(page_t, write_b);
    } else if (size == 2) {
        write_fn     = (void *) mem_write_ramw_page;
        write_offset = offsetof(page_t, write_w);
    } else {
        write_fn     = (void *) mem_write_raml_page;
        write_offset = offsetof(page_t, write_l);
    }

    /*codegen_in_recompile is never set here: compiled code does not run
      while a block is recorded*/
    /*The branches to C are short: the routine is in one allocator block*/

    /*Data the bytes hold already, as it most often is where code is written
      over with what it was: nothing to store and no dirty bits, as
      mem_write_ram*_page() does nothing more for it (and marks no idle loop
      written). The page's mem from page_lookup_mem[], without the page; 0
      for a page not written through those handlers, and for every page
      while a debug breakpoint is armed (the lookups are flushed as one is,
      and none are filled meanwhile: cpu_dr7_update())*/
    host_arm64_MOV_REG_LSR(block, REG_W2, REG_W0, 12);
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) page_lookup_mem);
    host_arm64_LDRX_REG_LSL3(block, REG_X5, REG_X3, REG_X2);
    not_known = host_arm64_CBZ_SHORT_(block, REG_X5);
    host_arm64_AND_IMM(block, REG_W6, REG_W0, 0xfff);
    if (size == 1) {
        host_arm64_AND_IMM(block, REG_W8, REG_W1, 0xff);
        host_arm64_LDRB_REG(block, REG_W7, REG_X5, REG_X6);
    } else if (size == 2) {
        host_arm64_AND_IMM(block, REG_W8, REG_W1, 0xffff);
        host_arm64_LDRH_REG(block, REG_W7, REG_X5, REG_X6);
    } else {
        host_arm64_MOV_REG(block, REG_W8, REG_W1, 0);
        host_arm64_LDR_REG(block, REG_W7, REG_X5, REG_X6);
    }
    host_arm64_CMP_REG(block, REG_W7, REG_W8);
    done[3] = host_arm64_BEQ_(block);
    /*Changed: the page is one the RAM handlers write, with its mem in X5
      (page_lookup_mem[] is filled only for such): the store, with the page
      in X4 for the dirty bits, unless an idle loop is watched*/
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) &mem_idle_track);
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X3, 0);
    to_c[nr_to_c++] = host_arm64_CBNZ_SHORT_(block, REG_W3);
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) page_lookup);
    host_arm64_LDRX_REG_LSL3(block, REG_X4, REG_X3, REG_X2);
    to_c[nr_to_c++] = host_arm64_CBZ_SHORT_(block, REG_X4);
    host_arm64_MOV_REG(block, REG_W1, REG_W8, 0);
    known_store = host_arm64_B_(block);
    host_arm64_short_branch_set_offset(not_known, &block_write_data[block_pos]);

    /*An armed debug breakpoint needs writememll()'s check*/
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) &cpu_dr_armed);
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X3, 0);
    to_c[nr_to_c++] = host_arm64_CBNZ_SHORT_(block, REG_W3);

    /*nor while an idle loop is watched: its writes reach mem_write_ram*_page()*/
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) &mem_idle_track);
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X3, 0);
    to_c[nr_to_c++] = host_arm64_CBNZ_SHORT_(block, REG_W3);

    /*X4 = page*/
    host_arm64_MOV_REG_LSR(block, REG_W2, REG_W0, 12);
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) page_lookup);
    host_arm64_LDRX_REG_LSL3(block, REG_X4, REG_X3, REG_X2);
    to_c[nr_to_c++] = host_arm64_CBZ_SHORT_(block, REG_X4);
    host_arm64_LDR_IMM_X(block, REG_X5, REG_X4, write_offset);
    host_arm64_MOVX_IMM(block, REG_X6, (uint64_t) write_fn);
    host_arm64_CMPX_REG(block, REG_X5, REG_X6);
    to_c[nr_to_c++] = host_arm64_BNE_SHORT_(block);

    /*X5 = page->mem*/
    host_arm64_LDR_IMM_X(block, REG_X5, REG_X4, offsetof(page_t, mem));
    to_c[nr_to_c++] = host_arm64_CBZ_SHORT_(block, REG_X5);
    host_arm64_MOVX_IMM(block, REG_X6, (uint64_t) page_ff);
    host_arm64_CMPX_REG(block, REG_X5, REG_X6);
    to_c[nr_to_c++] = host_arm64_BEQ_SHORT_(block);

    /*mem_logical_addr and high_page are left: only mapping handlers read
      them, and every access that reaches one sets them first*/

    /*Unchanged data: no store and no dirty bits. The C functions take the
      data as uint8_t or uint16_t, so only its low bits count.*/
    host_arm64_AND_IMM(block, REG_W6, REG_W0, 0xfff);
    if (size == 1) {
        host_arm64_AND_IMM(block, REG_W1, REG_W1, 0xff);
        host_arm64_LDRB_REG(block, REG_W7, REG_X5, REG_X6);
    } else if (size == 2) {
        host_arm64_AND_IMM(block, REG_W1, REG_W1, 0xffff);
        host_arm64_LDRH_REG(block, REG_W7, REG_X5, REG_X6);
    } else
        host_arm64_LDR_REG(block, REG_W7, REG_X5, REG_X6);
    host_arm64_CMP_REG(block, REG_W7, REG_W1);
    done[0] = host_arm64_BEQ_(block);
    host_arm64_branch_set_offset(known_store, &block_write_data[block_pos]);
    if (size == 1)
        host_arm64_STRB_REG(block, REG_W1, REG_X5, REG_X6);
    else if (size == 2)
        host_arm64_STRH_REG(block, REG_W1, REG_X5, REG_X6);
    else
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
    host_arm64_MOVZ_IMM(block, REG_X10, byte_mask);
    host_arm64_LSLX(block, REG_X10, REG_X10, REG_X9);
    host_arm64_MOV_REG(block, REG_W13, REG_W6, 3);
    host_arm64_LDR_IMM_X(block, REG_X11, REG_X4, offsetof(page_t, byte_dirty_mask));
    host_arm64_ADDX_REG_UXTW(block, REG_X11, REG_X11, REG_W13);
    host_arm64_LDR_IMM_X(block, REG_X12, REG_X11, 0);
    host_arm64_ORRX_REG(block, REG_X8, REG_X12, REG_X10, 0);
    host_arm64_STR_IMM_Q(block, REG_X8, REG_X11, 0);
    /*X7 = the bytes not dirty before*/
    host_arm64_EORX_REG(block, REG_X7, REG_X8, REG_X12);

    /*A store over a byte of code (byte_code_present) changed code: a
      byte-mask block leaves after it (cpu_state.code_written)*/
    host_arm64_LDR_IMM_X(block, REG_X12, REG_X4, offsetof(page_t, byte_code_present_mask));
    host_arm64_LDR_REG_X(block, REG_X12, REG_X12, REG_X13);
    host_arm64_TSTX_REG(block, REG_X12, REG_X10);
    no_code = host_arm64_BEQ_SHORT_(block);
    host_arm64_MOVZ_IMM(block, REG_W12, 1);
    host_arm64_STRB_IMM(block, REG_W12, REG_CPUSTATE, (uintptr_t) &cpu_state.code_written - (uintptr_t) &cpu_state);
    /*and, when it is the first to change those bytes since they were
      compiled, the cycles of a store that changes code (cpu_smc_penalty), as
      mem_write_ram*_page() charges them*/
    {
        uint32_t *not_first;

        host_arm64_TSTX_REG(block, REG_X12, REG_X7);
        not_first = host_arm64_BEQ_SHORT_(block);
        host_arm64_MOVX_IMM(block, REG_X12, (uint64_t) (uintptr_t) &cpu_smc_penalty);
        host_arm64_LDR_IMM_W(block, REG_W12, REG_X12, 0);
        host_arm64_SUB_REG(block, REG_CYCLES, REG_CYCLES, REG_W12, 0);
        host_arm64_short_branch_set_offset(not_first, &block_write_data[block_pos]);
    }
    host_arm64_short_branch_set_offset(no_code, &block_write_data[block_pos]);

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
    host_arm64_TST_IMM(block, REG_W12, byte_mask);
    done[2] = host_arm64_BEQ_(block);
    host_arm64_branch_set_offset(add_evict, &block_write_data[block_pos]);
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    host_arm64_MOVX_REG(block, REG_X0, REG_X4, 0);
    host_arm64_call(block, (void *) page_add_to_evict_list);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);

    for (int c = 0; c < 4; c++)
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
    uint32_t *misaligned_offset = NULL;
    uint8_t  *tracked_at;
    uint8_t  *c_at;

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
    /*All of it in one allocator block, so the branches to the slow path are
      single B.conds*/
    codegen_alloc(block, 640);
    host_arm64_MOV_REG_LSR(block, REG_W2, REG_W0, 12);
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) writelookup2);
    host_arm64_LDRX_REG_LSL3(block, REG_X2, REG_X3, REG_X2);
    if (size != 1) {
        host_arm64_TST_IMM(block, REG_W0, size - 1);
        misaligned_offset = host_arm64_BNE_SHORT_(block);
    }
    host_arm64_CMPX_IMM(block, REG_X2, -1);
    branch_offset = host_arm64_BEQ_SHORT_(block);
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

    host_arm64_short_branch_set_offset(branch_offset, &block_write_data[block_pos]);
    tracked_at = &block_write_data[block_pos];
#    if !defined(USE_GDBSTUB)
    if (size <= 4 && !is_float) {
        uint32_t *to_c[8];
        int       nr_to_c = build_store_tracked(block, size, to_c);

        for (int c = 0; c < nr_to_c; c++)
            host_arm64_short_branch_set_offset(to_c[c], &block_write_data[block_pos]);
    }
#    endif
    if (misaligned_offset)
        host_arm64_short_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);
    c_at = &block_write_data[block_pos];
    /*C may run a device's handler: the link routine checks again (cpu_state.link_c_ran)*/
    host_arm64_MOVZ_IMM(block, REG_W2, 1);
    host_arm64_STRB_IMM(block, REG_W2, REG_CPUSTATE, (uintptr_t) &cpu_state.link_c_ran - (uintptr_t) &cpu_state);
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

    /*The entry past the write lookup (codegen_mem_store_slow_*): misaligned
      to C, otherwise the tracked path, as the lookup's misses go*/
    if ((size <= 4) && !is_float) {
        void **slow = (size == 1) ? &codegen_mem_store_slow_byte : (size == 2) ? &codegen_mem_store_slow_word : &codegen_mem_store_slow_long;

        *slow = &block_write_data[block_pos];
        if (size != 1) {
            host_arm64_TST_IMM(block, REG_W0, size - 1);
            host_arm64_branch_set_offset(host_arm64_BNE_(block), c_at);
        }
        host_arm64_B(block, tracked_at);
    }
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
#    define REP_CYCLES  REG_CYCLES
#    define REP_END     REG_W14
#    define REP_STEP    REG_W15
#    define REP_ELEMCYC REG_W17
#    define REP_SLOW    REG_W8
#    define REP_RL      REG_X3
#    define REP_WL      REG_X4

#    define REP_MAX_PATCH 32

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

/*The guest registers, and with load_cycles REP_CYCLES (cycles is in it
  already as the routine is entered, and in cpu_state._cycles after C)*/
static void
rep_load_regs(codeblock_t *block, int is_stos, int a32, int load_cycles)
{
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
    if (load_cycles)
        host_arm64_LDR_IMM_W(block, REP_CYCLES, REG_CPUSTATE, rep_state_offset(&cpu_state._cycles));
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

/*Sets the slow flag if the segment at base_reg + offset would fail the
  interpreter's null selector or not-present check, and for the segment written
  its writability check (SEG_NOT_WRITABLE: writable bit clear, or a code segment
  in protected mode), using W2 = (msw & 1) && !(eflags & VM)*/
static void
rep_seg_check(codeblock_t *block, int base_reg, int offset, int write)
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
    if (write) {
        host_arm64_TST_IMM(block, REG_W5, 0x02);
        host_arm64_CSET_NE(block, REG_W6);
        host_arm64_EOR_IMM(block, REG_W6, REG_W6, 1);
        host_arm64_ORR_REG(block, REP_SLOW, REP_SLOW, REG_W6, 0);
        host_arm64_TST_IMM(block, REG_W5, 0x08);
        host_arm64_CSEL_EQ(block, REG_W6, REG_WZR, REG_W2);
        host_arm64_ORR_REG(block, REP_SLOW, REP_SLOW, REG_W6, 0);
    }
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
             cpu_state.pc set back to the instruction, or left at the next
             one, which the caller sets, after an SMI on the last element

  Does what the interpreter's REP handlers in x86_ops_rep_dyn.h do: the same
  cycles per element, and the block ends at the same element when the string
  outlasts the cycle limit or the trap flag is set. Elements are copied in
  runs that stay inside one source page and one destination page, both with
  direct lookups, inside both segment limits and inside the cycle budget, so a
  run does exactly what the interpreter's fast path does for each element.
  Any other element goes through run_func, which runs the interpreter's loop
  for it and the elements after it up to the next one a run can copy, the end
  of the count or the cycle limit, and leaves the registers and the cycle
  count in cpu_state.*/
/*A run of REP STOS into a page whose writes are tracked (it holds code):
  count elements of |step| bytes from lin, going down if step is negative,
  inside the page. Stored as the RAM write handlers would store each, and
  only the bytes it changes are marked dirty, as they mark them.*/
static void
rep_stos_tracked_fill(page_t *page, uint32_t lin, uint32_t count, int32_t step, uint32_t val)
{
    const uint32_t size  = (step < 0) ? -step : step;
    const uint32_t lo    = ((step < 0) ? (lin - (count - 1) * size) : lin) & 0xfff;
    const uint32_t bytes = count * size;
    uint8_t       *mem   = &page->mem[lo];
    int            first = -1;
    int            last  = -1;

    for (uint32_t c = 0; c < bytes; c++) {
        const uint8_t v = val >> (8 * (c & 3));

        if (mem[c] != v) {
            if (first < 0)
                first = c;
            last   = c;
            mem[c] = v;
        }
    }
    if (first >= 0)
        mem_phys_written(((uint32_t) (page - pages) << 12) | (lo + first), last - first + 1);
}

static void
build_rep_routine(codeblock_t *block, int is_stos, int shift, int a32, int (*run_func)(x86seg *seg, int32_t cycles_end))
{
    int       size = 1 << shift;
    int       es_offset = rep_state_offset(&cpu_state.seg_es);
    uint32_t *slow[REP_MAX_PATCH];
    int       nr_slow = 0;
    uint32_t *to_done;
    uint32_t *to_break[3] = { NULL, NULL, NULL };
    uint32_t *tracked     = NULL;
    uint32_t *to_abort;
    uint32_t *to_smi;
    uint32_t *branch;
    uint32_t *branch2;
    uint8_t  *loop;
    uint8_t  *target;
    uint8_t  *done_ret;
    uint8_t  *set_pc;

    /*Unpack the parameters*/
    host_arm64_UBFX(block, REG_W1, REG_W0, 12, 12);
    host_arm64_UBFX(block, REP_ELEMCYC, REG_W0, 24, 8);
    if (!is_stos) {
        host_arm64_AND_IMM(block, REG_W0, REG_W0, 0xfff);
        host_arm64_ADDX_REG_UXTW(block, REP_SEG, REG_CPUSTATE, REG_W0);
    }

    /*Guest registers*/
    rep_load_regs(block, is_stos, a32, 0);

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
    rep_seg_check(block, REG_CPUSTATE, es_offset, 1);
    if (!is_stos)
        rep_seg_check(block, REP_SEG, 0, 0);

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
    if (is_stos) {
        /*A page whose writes are tracked (it holds code) is filled through
          its RAM all the same, X5 = its page_t, and the run is marked
          dirty after it as a whole (rep_stos_tracked_mark()); X5 = 0 for a
          direct one. X3 = the run's first linear address.*/
        uint32_t *direct;

        host_arm64_MOVX_REG(block, REG_X5, REG_XZR, 0);
        host_arm64_MOV_REG(block, REG_W3, REG_W7, 0);
        host_arm64_CMPX_IMM(block, REG_X6, -1);
        direct = host_arm64_BNE_(block);
        host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) page_lookup);
        host_arm64_MOV_REG_LSR(block, REG_W6, REG_W7, 12);
        host_arm64_LDRX_REG_LSL3(block, REG_X5, REG_X2, REG_X6);
        host_arm64_CMPX_IMM(block, REG_X5, 0);
        slow[nr_slow++] = host_arm64_BEQ_(block);
        host_arm64_LDR_IMM_X(block, REG_X6, REG_X5, offsetof(page_t, mem));
        host_arm64_CMPX_IMM(block, REG_X6, 0);
        slow[nr_slow++] = host_arm64_BEQ_(block);
        host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) page_ff);
        host_arm64_CMPX_REG(block, REG_X6, REG_X2);
        slow[nr_slow++] = host_arm64_BEQ_(block);
        host_arm64_AND_IMM(block, REG_W2, REG_W7, 0xfffff000);
        host_arm64_SUBX_REG(block, REG_X6, REG_X6, REG_X2);
        host_arm64_branch_set_offset(direct, &block_write_data[block_pos]);
    } else {
        host_arm64_CMPX_IMM(block, REG_X6, -1);
        slow[nr_slow++] = host_arm64_BEQ_(block);
    }
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

    if (is_stos) {
        /*A tracked page is filled by rep_stos_tracked_fill()*/
        host_arm64_CMPX_IMM(block, REG_X5, 0);
        tracked = host_arm64_BNE_(block);
    }

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

    if (is_stos) {
        host_arm64_branch_set_offset(tracked, &block_write_data[block_pos]);
        host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
        host_arm64_STP_PREIDX_X(block, REG_X3, REG_X4, REG_XSP, -16);
        host_arm64_STP_PREIDX_X(block, REG_X8, REG_X9, REG_XSP, -16);
        host_arm64_STP_PREIDX_X(block, REG_X10, REG_X11, REG_XSP, -16);
        host_arm64_STP_PREIDX_X(block, REG_X12, REG_X13, REG_XSP, -16);
        host_arm64_STP_PREIDX_X(block, REG_X14, REG_X15, REG_XSP, -16);
        host_arm64_STP_PREIDX_X(block, REG_X17, REG_X1, REG_XSP, -16);
        host_arm64_MOV_REG(block, REG_W2, REG_W0, 0);
        host_arm64_MOVX_REG(block, REG_X0, REG_X5, 0);
        host_arm64_MOV_REG(block, REG_W1, REG_W3, 0);
        host_arm64_MOV_REG(block, REG_W3, REP_STEP, 0);
        host_arm64_MOV_REG(block, REG_W4, REP_VAL, 0);
        host_arm64_call(block, (void *) rep_stos_tracked_fill);
        host_arm64_LDP_POSTIDX_X(block, REG_X17, REG_X1, REG_XSP, 16);
        host_arm64_LDP_POSTIDX_X(block, REG_X14, REG_X15, REG_XSP, 16);
        host_arm64_LDP_POSTIDX_X(block, REG_X12, REG_X13, REG_XSP, 16);
        host_arm64_LDP_POSTIDX_X(block, REG_X10, REG_X11, REG_XSP, 16);
        host_arm64_LDP_POSTIDX_X(block, REG_X8, REG_X9, REG_XSP, 16);
        host_arm64_LDP_POSTIDX_X(block, REG_X3, REG_X4, REG_XSP, 16);
        host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
        host_arm64_CMP_REG(block, REP_CYCLES, REP_END);
        to_break[2] = host_arm64_BLT_(block);
        host_arm64_B(block, loop);
    }

    /*Elements through the interpreter's loop*/
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
    host_arm64_MOV_REG(block, REG_W1, REP_END, 0);
    host_arm64_call(block, (void *) run_func);
    host_arm64_LDP_POSTIDX_X(block, REG_X17, REG_X1, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X14, REG_X15, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X12, REG_X13, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X10, REG_X11, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X8, REG_X9, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X3, REG_X4, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    /*The call has stepped the guest registers and the cycle count*/
    rep_load_regs(block, is_stos, a32, 1);
    host_arm64_CMP_IMM(block, REG_W0, 0);
    to_abort = host_arm64_BNE_(block);
    /*An element that raised an SMI (a trapped write) ends the instruction,
      as the interpreter's loop does. Only these elements can: the runs copy
      RAM through direct lookups, which nothing traps.*/
    host_arm64_LDRB_IMM_W(block, REG_W0, REG_CPUSTATE, rep_state_offset(&cpu_state._smi_line));
    host_arm64_CMP_IMM(block, REG_W0, 0);
    to_smi = host_arm64_BNE_(block);
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
    rep_patch_list(to_break, is_stos ? 3 : 2, &block_write_data[block_pos]);
    rep_writeback(block, is_stos, a32);
    host_arm64_CMP_IMM(block, REP_CNT, 0);
    branch = host_arm64_BEQ_(block);
    host_arm64_branch_set_offset(branch, done_ret);
    branch = host_arm64_B_(block);

    /*Faulting element*/
    host_arm64_branch_set_offset(to_abort, &block_write_data[block_pos]);
    rep_writeback(block, is_stos, a32);

    host_arm64_branch_set_offset(branch, &block_write_data[block_pos]);
    set_pc = &block_write_data[block_pos];
    host_arm64_LDR_IMM_W(block, REG_W1, REG_CPUSTATE, rep_state_offset(&cpu_state.oldpc));
    host_arm64_STR_IMM_W(block, REG_W1, REG_CPUSTATE, rep_state_offset(&cpu_state.pc));
    host_arm64_mov_imm(block, REG_W0, 1);
    host_arm64_RET(block, REG_X30);

    /*SMI: leave the block for it, restarting the instruction if elements are
      left, otherwise after it, where the caller has set cpu_state.pc*/
    host_arm64_branch_set_offset(to_smi, &block_write_data[block_pos]);
    rep_writeback(block, is_stos, a32);
    host_arm64_CMP_IMM(block, REP_CNT, 0);
    branch = host_arm64_BNE_(block);
    host_arm64_branch_set_offset(branch, set_pc);
    host_arm64_mov_imm(block, REG_W0, 1);
    host_arm64_RET(block, REG_X30);

    if (nr_slow > REP_MAX_PATCH)
        fatal("build_rep_routine - too many slow path branches\n");
}

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
/*&codeblock[n] is codeblock + (n << CODEBLOCK_SHIFT)*/
#        define CODEBLOCK_SHIFT 8
_Static_assert(sizeof(codeblock_t) == (1 << CODEBLOCK_SHIFT), "a codeblock_t is 256 bytes on arm64");
#        define LINK_SLOT(field) ((int) offsetof(codegen_link_slot_t, field))
#        define LINK_MAX_MISS    32

/*The globals the link routine reads and writes, addressed from the PC when
  they are within ADRP's reach of the routine (every global in one page
  window, decided once when the routine is built), else loaded from a pool
  of addresses after the routine's code: one instruction each, whatever the
  address, so the routine fits its allocator block either way.*/
static const void *const link_globals[] = {
    &cpu_events, &nmi, &nmi_enable, &nmi_mask, &pic, &cpu_cur_status, &tsc, &tsc_old, &cycles_old,
    &timer_target, &get_phys_virt, &get_phys_phys, &oldcs, &oldcpl, &cpu_fetch_device, &rammask,
    readlookup2, &ram, &ioapic_irq_path_active, &cpu_lapic_irr_live, cpu_lapic_extint_live,
    &codeblock, _mem_exec, codegen_link_slots, &codegen_link_epoch, &codegen_link_state_st, &codegen_link_state_cr
};
static int link_adrp;

#        define LINK_MAX_POOL 32
static uint32_t   *link_pool_ref[LINK_MAX_POOL];
static const void *link_pool_ptr[LINK_MAX_POOL];
static int         link_pool_nr;

/*X<reg> = the page of ptr (ADRP) or ptr itself; returns the offset the load
  or store adds to it*/
static int
link_addr(codeblock_t *block, int reg, const void *ptr)
{
    if (link_adrp) {
        host_arm64_ADRP(block, reg, (uintptr_t) ptr);
        return (int) ((uintptr_t) ptr & 0xfff);
    }
    if (link_pool_nr >= LINK_MAX_POOL)
        fatal("build_link_routine - too many pooled addresses\n");
    host_arm64_LDR_LITERAL_X(block, reg, 0);
    link_pool_ref[link_pool_nr] = (uint32_t *) &block_write_data[block_pos - 4];
    link_pool_ptr[link_pool_nr++] = ptr;
    return 0;
}

/*The pool, after the routine's code and in its allocator block: one 8-byte
  entry per address, and every load pointed at its entry*/
static void
link_pool_emit(void)
{
    uint8_t *entry[LINK_MAX_POOL];

    block_pos = (block_pos + 7) & ~7;
    for (int c = 0; c < link_pool_nr; c++) {
        const uint64_t addr = (uintptr_t) link_pool_ptr[c];
        int            d;

        for (d = 0; d < c; d++) {
            if (link_pool_ptr[d] == link_pool_ptr[c])
                break;
        }
        if (d < c) {
            entry[c] = entry[d];
            continue;
        }
        if (block_pos + 8 > BLOCK_MAX)
            fatal("build_link_routine - address pool does not fit in the allocator block\n");
        entry[c] = &block_write_data[block_pos];
        memcpy(entry[c], &addr, 8);
        block_pos += 8;
    }
    for (int c = 0; c < link_pool_nr; c++)
        host_arm64_short_branch_set_offset(link_pool_ref[c], entry[c]);
}

/*The table record the link routine found last (its hint, codegen_link_slot_t)*/
static codegen_link_slot_t  link_table_hint_none;
/*The table records the link routine found last, the one before and the one
  before that: each is told of the records found after it, the next (the
  low half of its hint) and the third (the high half)*/
static codegen_link_slot_t *link_table_last[3] = { &link_table_hint_none, &link_table_hint_none, &link_table_hint_none };
/*The link routine's short way goes on only while cycles is above this
  (set by its full way; the C loop resets it to INT32_MAX each time it
  enters compiled code)*/
int32_t codegen_link_cycles_min = INT32_MAX;
/*Lines of the block's code fetched from its entry*/
#    define LINK_HINT_CODE_LINES 12

/*X<reg> = ptr itself*/
static void
link_addr_exact(codeblock_t *block, int reg, const void *ptr)
{
    int offset = link_addr(block, reg, ptr);

    if (offset)
        host_arm64_ADDX_IMM(block, reg, reg, offset);
}

/*The event flags are read as two 64-bit words and OR-ed into one test;
  halted is the top half of the last word and is shifted out of it*/
_Static_assert(offsetof(cpu_events_t, flush_pending) == 4 && offsetof(cpu_events_t, init) == 8
                   && offsetof(cpu_events_t, ne) == 12 && offsetof(cpu_events_t, force_interpreter) == 16
                   && offsetof(cpu_events_t, override_dynarec) == 20 && offsetof(cpu_events_t, dr_armed) == 24
                   && offsetof(cpu_events_t, halted) == 28 && offsetof(cpu_events_t, stpclk) == 48
                   && offsetof(cpu_events_t, throttle_run) == 52 && sizeof(cpu_events_t) == 56,
               "cpu_events is loaded as two pairs of 64-bit words");

/*Fields the routine loads together, with LDP or one wider load*/
_Static_assert(offsetof(codegen_link_slot_t, exit_cs) == offsetof(codegen_link_slot_t, exit_pc) + 4
                   && offsetof(codegen_link_slot_t, entry) == offsetof(codegen_link_slot_t, exit_pc) + 8,
               "exit_pc/exit_cs and entry are loaded as one pair");
_Static_assert(offsetof(codegen_link_slot_t, status) == offsetof(codegen_link_slot_t, gen) + 4
                   && offsetof(codegen_link_slot_t, npxc) == offsetof(codegen_link_slot_t, gen) + 6,
               "gen and status/npxc are loaded as one pair");
_Static_assert(offsetof(codegen_link_slot_t, phys_page) == offsetof(codegen_link_slot_t, cr0_bits) + 4
                   && !(offsetof(codegen_link_slot_t, cr0_bits) & 7),
               "cr0_bits and phys_page are the halves of one word");
_Static_assert(offsetof(codegen_link_slot_t, dirty_mask) == offsetof(codegen_link_slot_t, page_mask) + 8,
               "page_mask and dirty_mask are loaded as a pair");
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

  Globals are addressed from the PC (link_addr), so no load is spent on an
  address. Loads are paired wherever the layout allows, since on a hit the
  routine is limited by how many loads it issues rather than by its length.*/
/*Lines of the next block's code the link routine prefetches*/
#    define LINK_NEXT_CODE_LINES 16
/*Prefetch the first code, the codeblock_t line and the end slot of the
  block whose record is in X7. Uses X6, X8 and X9*/
static void
link_hint_prefetch(codeblock_t *block)
{
    int offset;

    host_arm64_LDR_IMM_X(block, REG_X8, REG_X7, LINK_SLOT(entry));
    for (int c = 0; c < LINK_HINT_CODE_LINES; c++)
        host_arm64_PRFM_I_IMM(block, REG_X8, c * 64);
    host_arm64_LDRH_IMM(block, REG_W8, REG_X7, LINK_SLOT(block_nr));
    offset = link_addr(block, REG_X6, &codeblock);
    host_arm64_LDR_IMM_X(block, REG_X6, REG_X6, offset);
    host_arm64_ADDX_REG(block, REG_X9, REG_X6, REG_X8, CODEBLOCK_SHIFT);
    host_arm64_PRFM_IMM(block, REG_X9, offsetof(codeblock_t, link_gen) & ~7);
    /*and the slot of its end*/
    host_arm64_LDRB_IMM_W(block, REG_W9, REG_X7, LINK_SLOT(end_slot));
    host_arm64_ADD_REG(block, REG_W8, REG_W8, REG_W8, 1);
    host_arm64_ADD_REG(block, REG_W8, REG_W8, REG_W9, 0);
    link_addr_exact(block, REG_X9, codegen_link_slots);
    host_arm64_ADDX_REG(block, REG_X8, REG_X9, REG_X8, 6);
    host_arm64_PRFM_IMM(block, REG_X8, 0);
}

/*X17 = the block about to run: used set, for the eviction sweep, and one
  run shifted off runs_left, which counts the runs that pay for compiling
  it. Read as one word with the byte between them: once used is set and
  runs_left is 0 nothing is stored. Uses W4 and W5*/
_Static_assert((offsetof(codeblock_t, used) == 4) && (offsetof(codeblock_t, runs_left) == 6), "used and runs_left are read as the word at 4");
static void
link_used_emit(codeblock_t *block)
{
    uint32_t *settled;

    host_arm64_LDR_IMM_W(block, REG_W4, REG_X17, offsetof(codeblock_t, used));
    host_arm64_CMP_IMM(block, REG_W4, 1);
    settled = host_arm64_BEQ_SHORT_(block);
    host_arm64_MOVZ_IMM(block, REG_W5, 1);
    host_arm64_STRB_IMM(block, REG_W5, REG_X17, offsetof(codeblock_t, used));
    host_arm64_MOV_REG_LSR(block, REG_W4, REG_W4, 17);
    host_arm64_STRH_IMM(block, REG_W4, REG_X17, offsetof(codeblock_t, runs_left));
    host_arm64_short_branch_set_offset(settled, &block_write_data[block_pos]);
}

static void
build_link_routine(codeblock_t *block)
{
    uint8_t  *start_data;
    uint32_t *miss[LINK_MAX_MISS];
    int       nr_miss = 0;
    uint32_t *key_miss;
    uint32_t *nmi_set;
    uint32_t *irq_ok;
    uint32_t *apic_set;
    uint32_t *uncached;
    uint32_t *no_paging;
    uint32_t *cached;
    uint32_t *two_page;
    uint32_t *top_check;
    uint32_t *keep_oldcs;
    uint8_t  *top_done;
    uint32_t *one_page;
    uint32_t *byte_mask;
    uint32_t *no_paging_2;
    uint8_t  *nmi_done;
    uint8_t  *irq_done;
    uint8_t  *xlat_done;
    uint8_t  *two_page_done;
    uint8_t  *xlat_2_done;
    uint8_t  *miss_dest;
    uint32_t *epoch_xlat;
    uint32_t *epoch_store;
    uint32_t *epoch_page2;
    uint32_t *state_full;
    uint32_t *state_full_2;
    uint32_t *state_skip;
    int       off_tsc;
    int       off_tsc_old;
    int       off_cycles_old;
    int       off_virt;
    int       off_phys;
    int       offset;

    /*Start a fresh allocator block, so nothing below is split across two*/
    codegen_alloc(block, BLOCK_MAX - 4);
    start_data = block_write_data;

    link_adrp = 1;
    for (size_t c = 0; c < sizeof(link_globals) / sizeof(link_globals[0]); c++) {
        if (!host_arm64_ADRP_in_range((uintptr_t) link_globals[c] - 0x1000)
            || !host_arm64_ADRP_in_range((uintptr_t) link_globals[c] + 0x1000))
            link_adrp = 0;
    }
    link_pool_nr = 0;

    codegen_link_rout = &block_write_data[block_pos];

    /*X1 = the exit's slot, whose exit_pc and exit_cs must match cpu_state.pc
      and the CS base and whose entry must be set; X15 = its entry;
      W2 = cs + pc*/
    host_arm64_LDR_IMM_W(block, REG_W2, REG_CPUSTATE, rep_state_offset(&cpu_state.pc));
    host_arm64_LDR_IMM_W(block, REG_W3, REG_CPUSTATE, rep_state_offset(&cpu_state.seg_cs.base));
    host_arm64_LDP_IMM_X(block, REG_X5, REG_X15, REG_X0, LINK_SLOT(exit_pc));
    host_arm64_ORRX_REG(block, REG_X4, REG_X2, REG_X3, 32);
    host_arm64_ADD_REG(block, REG_W2, REG_W2, REG_W3, 0);
    host_arm64_MOVX_REG(block, REG_X1, REG_X0, 0);
    host_arm64_CMPX_REG(block, REG_X5, REG_X4);
    key_miss        = host_arm64_BNE_SHORT_(block);
    link_set_return = &block_write_data[block_pos];
    miss[nr_miss++] = host_arm64_CBZ_SHORT_(block, REG_X15);
    /*A record whose checks mostly cannot have changed (two_page bit 5) takes
      the short way, in the set routine's block (build_link_set_routine()),
      which comes back here when they may have*/
    {
        uint32_t *full;

        host_arm64_LDRB_IMM_W(block, REG_W10, REG_X1, LINK_SLOT(two_page));
        full             = host_arm64_TBZ_SHORT_(block, REG_W10, 5);
        link_fast_branch = host_arm64_B_(block);
        host_arm64_short_branch_set_offset(full, &block_write_data[block_pos]);
    }
    link_fast_normal = &block_write_data[block_pos];

    /*W16 = 0 if the record passed every check at this codegen_link_epoch:
      the target block, its page's translation and mem_exec are as they were
      then, and are not checked again. Kept to the stores*/
    offset = link_addr(block, REG_X16, &codegen_link_epoch);
    host_arm64_LDR_IMM_W(block, REG_W16, REG_X16, offset);
    host_arm64_LDR_IMM_W(block, REG_W13, REG_X1, LINK_SLOT(epoch));
    host_arm64_EOR_REG(block, REG_W16, REG_W16, REG_W13, 0);

    /*Target not freed or changed since the slot was filled. W6 = the slot's
      status | npxc << 16*/
    /*X17 = the block, &codeblock[block_nr], kept to the end*/
    host_arm64_LDRH_IMM(block, REG_W17, REG_X1, LINK_SLOT(block_nr));
    offset = link_addr(block, REG_X4, &codeblock);
    host_arm64_LDR_IMM_X(block, REG_X4, REG_X4, offset);
    host_arm64_ADDX_REG(block, REG_X17, REG_X4, REG_X17, CODEBLOCK_SHIFT);
    /*On every link: a block's change moves its link_gen on, not the epoch*/
    host_arm64_LDP_IMM_W(block, REG_W5, REG_W6, REG_X1, LINK_SLOT(gen));
    host_arm64_LDR_IMM_W(block, REG_W4, REG_X17, offsetof(codeblock_t, link_gen));
    host_arm64_CMP_REG(block, REG_W4, REG_W5);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);

    /*The exit's block changes none of the events, NMI, interrupt and trap
      flag state checked next (two_page bit 2), and nothing has called C since
      they were last checked: they are as they were then. Not the status,
      which is compared with the target's. W14 = cycles, X13 = CR0 |
      flags << 32, as those checks leave them*/
    host_arm64_MOV_REG(block, REG_W14, REG_CYCLES, 0);
    host_arm64_LDR_IMM_X(block, REG_X13, REG_CPUSTATE, rep_state_offset(&cpu_state.CR0.l));
    host_arm64_LDRB_IMM_W(block, REG_W10, REG_X1, LINK_SLOT(two_page));
    state_full = host_arm64_TBZ_SHORT_(block, REG_W10, 2);
    host_arm64_LDRB_IMM_W(block, REG_W10, REG_CPUSTATE, rep_state_offset(&cpu_state.link_c_ran));
    state_full_2 = host_arm64_CBNZ_SHORT_(block, REG_W10);
    state_skip   = host_arm64_B_(block);
    host_arm64_short_branch_set_offset(state_full, &block_write_data[block_pos]);
    host_arm64_short_branch_set_offset(state_full_2, &block_write_data[block_pos]);

    /*Nothing pending for the loop, and the loop would pick the recompiler.
      An armed debug breakpoint is a miss, so the loop makes its per-block
      check (dr_blocks_cache()). The seven event flags are the two pairs
      loaded from cpu_events, with halted shifted out of the last word;
      W7 = abrt and smi_line in its top half, W14 = cycles. Clock control
      (stpclk and throttle_run, X10) is a miss too: the loop stops the
      processor or stalls it after each block while it is on, and a
      P_LVL2/P_LVL3 read by INS does not end its block. Both are set from C
      only, so the short way, taken only while no C has run since this
      check passed, needs no test of its own*/
    /*Two loads, not a pair: the block has just stored cycles, and a load
      wider than that store waits for it to reach the cache*/
    host_arm64_LDR_IMM_W(block, REG_W7, REG_CPUSTATE, rep_state_offset(&cpu_state.abrt) - 2);
    link_addr_exact(block, REG_X9, &cpu_events);
    host_arm64_LDP_IMM_X(block, REG_X3, REG_X4, REG_X9, 0);
    host_arm64_LDP_IMM_X(block, REG_X5, REG_X8, REG_X9, 16);
    host_arm64_LDR_IMM_X(block, REG_X10, REG_X9, offsetof(cpu_events_t, stpclk));
    offset = link_addr(block, REG_X12, &nmi);
    host_arm64_LDR_IMM_W(block, REG_W12, REG_X12, offset);
    host_arm64_AND_IMM(block, REG_W7, REG_W7, 0xffff0000);
    host_arm64_ORRX_REG(block, REG_X3, REG_X3, REG_X10, 0);
    host_arm64_ORRX_REG(block, REG_X3, REG_X3, REG_X4, 0);
    host_arm64_ORRX_REG(block, REG_X5, REG_X5, REG_X7, 0);
    host_arm64_ORRX_REG(block, REG_X3, REG_X3, REG_X8, 32);
    host_arm64_ORRX_REG(block, REG_X3, REG_X3, REG_X5, 0);
    miss[nr_miss++] = host_arm64_CBNZ_SHORT_(block, REG_X3);

    /*NMI_TAKEABLE(): nmi here, the rest out of line*/
    nmi_set  = host_arm64_CBNZ_SHORT_(block, REG_W12);
    nmi_done = &block_write_data[block_pos];

    /*Trap flag, and an interrupt the loop would take. With the APIC path
      off x86_external_interrupt_pending() is pic.int_pending; with it on
      it is 0 while pic.int_pending, local APIC 0's ExtINT and its IRR bit
      are all clear, and anything else is left to the loop (out of line).
      X13 = CR0 | flags << 32*/
    miss[nr_miss++] = host_arm64_TBNZ_SHORT_(block, REG_X13, 32 + 8);
    irq_ok          = host_arm64_TBZ_SHORT_(block, REG_X13, 32 + 9);
    offset          = link_addr(block, REG_X3, &pic.int_pending);
    host_arm64_LDRB_IMM_W(block, REG_W5, REG_X3, offset);
    miss[nr_miss++] = host_arm64_CBNZ_SHORT_(block, REG_W5);
    offset          = link_addr(block, REG_X3, &ioapic_irq_path_active);
    host_arm64_LDRB_IMM_W(block, REG_W5, REG_X3, offset);
    apic_set        = host_arm64_CBNZ_SHORT_(block, REG_W5);
    irq_done        = &block_write_data[block_pos];
    host_arm64_short_branch_set_offset(irq_ok, irq_done);
    /*Checked: until compiled code calls C again*/
    host_arm64_STRB_IMM(block, REG_WZR, REG_CPUSTATE, rep_state_offset(&cpu_state.link_c_ran));
    host_arm64_branch_set_offset(state_skip, &block_write_data[block_pos]);

    /*Status and FPU rounding as when the slot was filled, compared as one
      word against W6. Only the C loop refreshes the PC24 and RC_NZ status
      bits from npxc, so they can be stale after an FLDCW in a linked block;
      the npxc compare decides them instead. Everything else that reads
      those two bits runs after that refresh. NOTFLATES is compared as
      stored, since every write to the ES base or limits brings it in line
      before the next lookup.*/
    offset = link_addr(block, REG_X4, &cpu_cur_status);
    host_arm64_LDRH_IMM(block, REG_W4, REG_X4, offset);
    host_arm64_LDRH_IMM(block, REG_W5, REG_CPUSTATE, rep_state_offset(&cpu_state.npxc));
    host_arm64_AND_IMM(block, REG_W5, REG_W5, 0x0f00);
    host_arm64_ORR_REG(block, REG_W4, REG_W4, REG_W5, 16);
    /*as the short way compares records with it (codegen_link_state_st),
      without the bits the compare leaves out*/
    host_arm64_AND_IMM(block, REG_W4, REG_W4, ~(uint32_t) (CPU_STATUS_FPU_PC24 | CPU_STATUS_FPU_RC_NZ));
    offset = link_addr(block, REG_X5, &codegen_link_state_st);
    host_arm64_STR_IMM_W(block, REG_W4, REG_X5, offset);
    host_arm64_EOR_REG(block, REG_W4, REG_W4, REG_W6, 0);
    host_arm64_TST_IMM(block, REG_W4, ~(uint32_t) (CPU_STATUS_FPU_PC24 | CPU_STATUS_FPU_RC_NZ));
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);

    /*The TSC, as the C loop works it out after a block. X3 = tsc's page, X11 = new tsc,
      X4 = tsc_old's page, X5 = cycles_old's page (kept for the stores)*/
    off_tsc        = link_addr(block, REG_X3, &tsc);
    off_tsc_old    = link_addr(block, REG_X4, &tsc_old);
    off_cycles_old = link_addr(block, REG_X5, &cycles_old);
    host_arm64_LDR_IMM_X(block, REG_X11, REG_X3, off_tsc);
    host_arm64_LDR_IMM_X(block, REG_X7, REG_X4, off_tsc_old);
    host_arm64_LDR_IMM_W(block, REG_W8, REG_X5, off_cycles_old);
    host_arm64_SUB_REG(block, REG_W8, REG_W8, REG_W14, 0);
    host_arm64_SUBX_REG(block, REG_X7, REG_X11, REG_X7);
    {
        uint32_t *unmoved;
        uint32_t *moved_done;

        unmoved = host_arm64_CBZ_SHORT_(block, REG_X7);
        /*It moved: update_tsc() ran inside the block (RDTSC, a timer read
          on a port) and counted that much of the time. The rest is added,
          as the C loop would (cycdiff - delta, if above 0); a TSC set
          further back or ahead (WRMSR) is left to the C loop*/
        host_arm64_LSR64_32(block, REG_X9, REG_X7);
        miss[nr_miss++] = host_arm64_CBNZ_SHORT_(block, REG_X9);
        host_arm64_SUB_REG(block, REG_W8, REG_W8, REG_W7, 0);
        host_arm64_CMP_IMM(block, REG_W8, 0);
        host_arm64_CSEL_GT(block, REG_W8, REG_W8, REG_WZR);
        host_arm64_ADDX_REG_UXTW(block, REG_X11, REG_X11, REG_W8);
        moved_done = host_arm64_B_(block);
        host_arm64_short_branch_set_offset(unmoved, &block_write_data[block_pos]);
        host_arm64_CMP_IMM(block, REG_W8, 0);
        miss[nr_miss++] = host_arm64_BLE_SHORT_(block);
        host_arm64_ADDX_REG_UXTW(block, REG_X11, REG_X11, REG_W8);
        host_arm64_branch_set_offset(moved_done, &block_write_data[block_pos]);
    }
    /*No timer due at the new TSC*/
    offset = link_addr(block, REG_X6, &timer_target);
    host_arm64_LDR_IMM_X(block, REG_X7, REG_X6, offset);
    host_arm64_SUBX_REG(block, REG_X7, REG_X7, REG_X11);
    host_arm64_CMPX_IMM(block, REG_X7, 0);
    miss[nr_miss++] = host_arm64_BLE_SHORT_(block);
    /*Slice not used up*/
    host_arm64_CMP_IMM(block, REG_W14, 0);
    miss[nr_miss++] = host_arm64_BLE_SHORT_(block);
    /*For the short way: cycles must stay above this for the slice to last
      and the next timer to be ahead, W14 - (timer_target - new tsc) or 0*/
    host_arm64_CMPX_REG(block, REG_X14, REG_X7);
    host_arm64_SUBX_REG(block, REG_X8, REG_X14, REG_X7);
    host_arm64_CSELX_GT(block, REG_X8, REG_X8, REG_XZR);
    offset = link_addr(block, REG_X10, &codegen_link_cycles_min);
    host_arm64_STR_IMM_W(block, REG_W8, REG_X10, offset);

    /*Paging and cache-disable bits, and the CPL, as when the slot was filled. W9 = them,
      W12 = phys_page (the upper halves hold virt and rammask)*/
    host_arm64_LDR_IMM_X(block, REG_X9, REG_X1, LINK_SLOT(cr0_bits));
    host_arm64_LSR64_32(block, REG_X12, REG_X9);
    host_arm64_AND_IMM(block, REG_W10, REG_W13, 0xc0000000);
    /*and the CPL, from CS's access byte*/
    host_arm64_LDRB_IMM_W(block, REG_W8, REG_CPUSTATE, rep_state_offset(&cpu_state.seg_cs.access));
    host_arm64_UBFX(block, REG_W8, REG_W8, 5, 2);
    host_arm64_ORR_REG(block, REG_W10, REG_W10, REG_W8, 0);
    offset = link_addr(block, REG_X8, &codegen_link_state_cr);
    host_arm64_STR_IMM_W(block, REG_W10, REG_X8, offset);
    host_arm64_CMP_REG(block, REG_W10, REG_W9);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);

    /*get_phys(cs + pc) gives phys_page. X6 = get_phys_virt's page,
      X7 = get_phys_phys's page (kept for the stores), W8 = nonzero if
      get_phys() would miss its cache*/
    host_arm64_MOVZ_IMM(block, REG_W8, 0);
    epoch_xlat = host_arm64_CBZ_SHORT_(block, REG_W16);
    off_virt = link_addr(block, REG_X6, &get_phys_virt);
    off_phys = link_addr(block, REG_X7, &get_phys_phys);
    host_arm64_LDR_IMM_W(block, REG_W8, REG_X6, off_virt);
    host_arm64_EOR_REG(block, REG_W8, REG_W8, REG_W2, 0);
    host_arm64_ANDS_IMM(block, REG_W8, REG_W8, 0xfffff000);
    uncached = host_arm64_BNE_SHORT_(block);
    host_arm64_LDR_IMM_W(block, REG_W10, REG_X7, off_phys);
    host_arm64_CMP_REG(block, REG_W10, REG_W12);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    xlat_done = &block_write_data[block_pos];
    /*_mem_exec[phys_page >> 12]*/
    _Static_assert(MEM_GRANULARITY_BITS == 12, "mem_exec is indexed by page");
    link_addr_exact(block, REG_X10, _mem_exec);
    host_arm64_MOV_REG_LSR(block, REG_W13, REG_W12, 12);
    host_arm64_LDRX_REG_LSL3(block, REG_X10, REG_X10, REG_X13);
    miss[nr_miss++] = host_arm64_CBZ_SHORT_(block, REG_X10);
    host_arm64_short_branch_set_offset(epoch_xlat, &block_write_data[block_pos]);

    /*The checks made before running the target. X9 = block_nr | top_mask << 16 |
      top << 24 | two_page << 32. The C loop's lookup is not repeated: its
      hash entry is only a cache, which a block 128K away in physical memory
      takes over (HASH()), and on a hash miss the loop finds a block in the
      page's tree. The target is still one the loop could run, which is all
      a hit needs: link_gen is unchanged, so it has not been freed,
      recompiled or moved to the dirty list since the slot was filled, and
      its key, status and pages are checked here. Checking the hash entry
      too would send every exit to such a target back through the loop's
      tree walk while two hot blocks share an entry.*/
    host_arm64_LDR_IMM_X(block, REG_X9, REG_X1, LINK_SLOT(block_nr));
    host_arm64_LDP_IMM_X(block, REG_X10, REG_X13, REG_X1, LINK_SLOT(page_mask));
    host_arm64_LDR_IMM_X(block, REG_X13, REG_X13, 0);
    host_arm64_TSTX_REG(block, REG_X13, REG_X10);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    /*A static TOP is checked out of line (top_check): most targets have none*/
    host_arm64_TST_IMM(block, REG_W9, 0x00ff0000);
    top_check = host_arm64_BNE_SHORT_(block);
    top_done  = &block_write_data[block_pos];
    two_page      = host_arm64_TBNZ_SHORT_(block, REG_X9, 32);
    two_page_done = &block_write_data[block_pos];

    /*Every check passed, at this epoch from now on (none since the last
      bump if W16 is 0; the two-page path may have changed it, which at
      worst stores the same epoch)*/
    epoch_store = host_arm64_CBZ_SHORT_(block, REG_W16);
    offset = link_addr(block, REG_X13, &codegen_link_epoch);
    host_arm64_LDR_IMM_W(block, REG_W13, REG_X13, offset);
    host_arm64_STR_IMM_W(block, REG_W13, REG_X1, LINK_SLOT(epoch));
    host_arm64_short_branch_set_offset(epoch_store, &block_write_data[block_pos]);

    /*Every check passed: the C loop's stores, then the next block. W10 =
      CS access | selector << 16*/
    host_arm64_STR_IMM_Q(block, REG_X11, REG_X3, off_tsc);
    host_arm64_STR_IMM_Q(block, REG_X11, REG_X4, off_tsc_old);
    host_arm64_STR_IMM_W(block, REG_W14, REG_X5, off_cycles_old);
    /*oldcs and oldcpl as at a loop top, unless the exit's block calls no C,
      so that CS and the CPL are as the last store left them (two_page bit 1)*/
    host_arm64_LDRB_IMM_W(block, REG_W10, REG_X1, LINK_SLOT(two_page));
    keep_oldcs = host_arm64_TBNZ_SHORT_(block, REG_W10, 1);
    host_arm64_LDR_IMM_W(block, REG_W10, REG_CPUSTATE, rep_state_offset(&cpu_state.seg_cs.access));
    offset = link_addr(block, REG_X3, &oldcs);
    host_arm64_UBFX(block, REG_W13, REG_W10, 16, 16);
    host_arm64_STRH_IMM(block, REG_W13, REG_X3, offset);
    offset = link_addr(block, REG_X4, &oldcpl);
    host_arm64_UBFX(block, REG_W10, REG_W10, 5, 2);
    host_arm64_STR_IMM_W(block, REG_W10, REG_X4, offset);
    /*cpu_fetch_device and cpu_halted, cleared as the C loop does: only C
      sets them, so a block that calls none left them clear*/
    offset = link_addr(block, REG_X3, &cpu_fetch_device);
    host_arm64_STR_IMM_W(block, REG_WZR, REG_X3, offset);
    offset = link_addr(block, REG_X4, &cpu_events);
    host_arm64_STR_IMM_W(block, REG_WZR, REG_X4, offset + offsetof(cpu_events_t, halted));
    host_arm64_short_branch_set_offset(keep_oldcs, &block_write_data[block_pos]);
    /*get_phys() only writes its cache when it misses*/
    cached = host_arm64_CBZ_SHORT_(block, REG_W8);
    host_arm64_STR_IMM_W(block, REG_W2, REG_X6, off_virt);
    host_arm64_STR_IMM_W(block, REG_W12, REG_X7, off_phys);
    host_arm64_short_branch_set_offset(cached, &block_write_data[block_pos]);
    /*The block is about to run: the eviction sweep passes it over once, and
      the run counts towards its compile's cost (link_used_emit())*/
    link_used_emit(block);
    _Static_assert((sizeof(codegen_link_slot_t) == 64) && (CODEGEN_LINK_SLOTS == 3), "a slot is one line, three a block");
    /*The block the target's end went to last is likely the one after it:
      its code, from memory when the code in use is too much for the
      caches, comes in while the target runs*/
    /*Out of line, in the set routine's block, which then goes to X15
      (build_link_set_routine()): only for a target long enough to hide the
      fetch, which a short block in a hot loop never needs (two_page bit 3,
      CODEGEN_LINK_NEXT_MIN_INS)*/
    {
        uint32_t *short_block;

        host_arm64_LDRB_IMM_W(block, REG_W4, REG_X1, LINK_SLOT(two_page));
        short_block      = host_arm64_TBZ_SHORT_(block, REG_W4, 3);
        link_next_branch = host_arm64_B_(block);
        host_arm64_short_branch_set_offset(short_block, &block_write_data[block_pos]);
    }
    host_arm64_BR(block, REG_X15);
    /*The static TOP check is in the set routine's block (build_link_set_routine())*/
    link_top_check = top_check;
    link_top_done  = top_done;

    /*Miss: the block's own epilogue, X0 still the slots. codegen_exit_rout
      past its X0 = 0 is that epilogue.*/
    miss_dest = &block_write_data[block_pos];
    link_miss_dest = miss_dest;
    host_arm64_B(block, codegen_link_miss_rout);

    /*Key mismatch: the slot's set (build_link_set_routine), in another
      allocator block*/
    host_arm64_short_branch_set_offset(key_miss, &block_write_data[block_pos]);
    link_set_branch = host_arm64_B_(block);

    /*nmi set and the APIC path: out of line, in the set routine's
      allocator block (build_link_set_routine())*/
    link_nmi_set  = nmi_set;
    link_nmi_done = nmi_done;
    link_apic_set = apic_set;
    link_irq_done = irq_done;

    /*get_phys() cache miss: it would store the full address in
      get_phys_virt, and translate through readlookup2[] with paging on or
      through rammask with paging off. The page that gives must be the
      slot's (W12); a readlookup2[] miss would make get_phys() walk the page
      tables, so it is a miss here*/
    host_arm64_short_branch_set_offset(uncached, &block_write_data[block_pos]);
    no_paging = host_arm64_TBZ_SHORT_(block, REG_W9, 31);
    link_addr_exact(block, REG_X10, readlookup2);
    host_arm64_MOV_REG_LSR(block, REG_W13, REG_W2, 12);
    host_arm64_LDRX_REG_LSL3(block, REG_X10, REG_X10, REG_X13);
    host_arm64_CMN_IMM(block, REG_W10, 1);
    miss[nr_miss++] = host_arm64_BEQ_SHORT_(block);
    host_arm64_AND_IMM(block, REG_W13, REG_W2, 0xfffff000);
    host_arm64_ADDX_REG_UXTW(block, REG_X10, REG_X10, REG_W13);
    offset = link_addr(block, REG_X13, &ram);
    host_arm64_LDR_IMM_X(block, REG_X13, REG_X13, offset);
    host_arm64_SUBX_REG(block, REG_X10, REG_X10, REG_X13);
    host_arm64_CMP_REG(block, REG_W10, REG_W12);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_B(block, xlat_done);
    host_arm64_short_branch_set_offset(no_paging, &block_write_data[block_pos]);
    offset = link_addr(block, REG_X10, &rammask);
    host_arm64_LDR_IMM_W(block, REG_W10, REG_X10, offset);
    host_arm64_AND_REG(block, REG_W10, REG_W10, REG_W2, 0);
    host_arm64_AND_IMM(block, REG_W10, REG_W10, 0xfffff000);
    host_arm64_CMP_REG(block, REG_W10, REG_W12);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_B(block, xlat_done);

    /*Target spans two pages: the second page's dirty mask, and its page from
      get_phys_noabrt() at the address the C loop takes from the block's pc,
      against phys_2. The block's own fields are read as the C loop reads
      them. A readlookup2[] miss would make get_phys_noabrt() walk the page
      tables, so it is a miss here. X9 = the block, W10 = the address,
      W13 = its physical address*/
    host_arm64_short_branch_set_offset(two_page, &block_write_data[block_pos]);
    host_arm64_MOVX_REG(block, REG_X9, REG_X17, 0);
    host_arm64_LDR_IMM_X(block, REG_X10, REG_X9, offsetof(codeblock_t, page_mask2));
    one_page = host_arm64_CBZ_SHORT_(block, REG_X10);
    host_arm64_short_branch_set_offset(one_page, two_page_done);
    host_arm64_LDR_IMM_X(block, REG_X13, REG_X9, offsetof(codeblock_t, dirty_mask2));
    host_arm64_LDR_IMM_X(block, REG_X13, REG_X13, 0);
    host_arm64_TSTX_REG(block, REG_X13, REG_X10);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    /*The second page's translation is as at the record's epoch (W16 = 0)*/
    epoch_page2 = host_arm64_CBZ_SHORT_(block, REG_W16);
    host_arm64_short_branch_set_offset(epoch_page2, two_page_done);
    /*pc_2, or for a byte-mask block the next 64 bytes*/
    host_arm64_LDR_IMM_W(block, REG_W10, REG_X9, offsetof(codeblock_t, pc_2));
    host_arm64_LDRH_IMM(block, REG_W13, REG_X9, offsetof(codeblock_t, flags));
    byte_mask = host_arm64_TBZ_SHORT_(block, REG_W13, 5);
    host_arm64_LDR_IMM_W(block, REG_W10, REG_X9, offsetof(codeblock_t, pc));
    host_arm64_ADD_IMM(block, REG_W10, REG_W10, 0x40);
    host_arm64_short_branch_set_offset(byte_mask, &block_write_data[block_pos]);
    host_arm64_LDR_IMM_W(block, REG_W13, REG_X1, LINK_SLOT(cr0_bits));
    no_paging_2 = host_arm64_TBZ_SHORT_(block, REG_W13, 31);
    link_addr_exact(block, REG_X13, readlookup2);
    host_arm64_UBFX(block, REG_W16, REG_W10, 12, 20);
    host_arm64_LDRX_REG_LSL3(block, REG_X13, REG_X13, REG_X16);
    host_arm64_CMN_IMM(block, REG_W13, 1);
    miss[nr_miss++] = host_arm64_BEQ_SHORT_(block);
    offset = link_addr(block, REG_X16, &ram);
    host_arm64_LDR_IMM_X(block, REG_X16, REG_X16, offset);
    host_arm64_ADDX_REG_UXTW(block, REG_X13, REG_X13, REG_W10);
    host_arm64_SUBX_REG(block, REG_X13, REG_X13, REG_X16);
    xlat_2_done = &block_write_data[block_pos];
    host_arm64_LDR_IMM_W(block, REG_W16, REG_X9, offsetof(codeblock_t, phys_2));
    host_arm64_EOR_REG(block, REG_W16, REG_W16, REG_W13, 0);
    host_arm64_TST_IMM(block, REG_W16, 0xfffff000);
    miss[nr_miss++] = host_arm64_BNE_SHORT_(block);
    host_arm64_B(block, two_page_done);
    host_arm64_short_branch_set_offset(no_paging_2, &block_write_data[block_pos]);
    offset = link_addr(block, REG_X16, &rammask);
    host_arm64_LDR_IMM_W(block, REG_W16, REG_X16, offset);
    host_arm64_AND_REG(block, REG_W13, REG_W10, REG_W16, 0);
    host_arm64_B(block, xlat_2_done);

    if (link_pool_nr)
        link_pool_emit();

    if (nr_miss > LINK_MAX_MISS)
        fatal("build_link_routine - too many miss branches\n");
    /*The link routine's entry for an exit from a block whose CS is the one
      it is keyed on, as every slot of the block was filled with: only the
      pc is compared, and on a mismatch the routine's start takes it. W2 =
      cs + pc, X1 = the slot, X15 = its entry, as its compare leaves them*/
    {
        uint32_t *other;

        codegen_alloc(block, 48);
        codegen_link_rout_direct = &block_write_data[block_pos];
        host_arm64_LDR_IMM_W(block, REG_W2, REG_CPUSTATE, rep_state_offset(&cpu_state.pc));
        host_arm64_LDR_IMM_W(block, REG_W4, REG_X0, LINK_SLOT(exit_pc));
        host_arm64_LDR_IMM_X(block, REG_X15, REG_X0, LINK_SLOT(entry));
        host_arm64_CMP_REG(block, REG_W2, REG_W4);
        other = host_arm64_BNE_SHORT_(block);
        host_arm64_LDR_IMM_W(block, REG_W3, REG_CPUSTATE, rep_state_offset(&cpu_state.seg_cs.base));
        host_arm64_MOVX_REG(block, REG_X1, REG_X0, 0);
        host_arm64_ADD_REG(block, REG_W2, REG_W2, REG_W3, 0);
        host_arm64_B(block, link_set_return);
        host_arm64_short_branch_set_offset(other, &block_write_data[block_pos]);
        host_arm64_B(block, codegen_link_rout);
    }

    if (block_write_data != start_data)
        fatal("build_link_routine - routine does not fit in one allocator block\n");
    for (int c = 0; c < nr_miss; c++)
        host_arm64_short_branch_set_offset(miss[c], miss_dest);
}

_Static_assert((sizeof(codegen_link_slot_t) == 64) && (sizeof(codegen_link_set_t) == 256) && !(offsetof(codegen_link_set_t, e) & 63),
               "the set lookup copies a slot as four pairs of words, and finds a set as 256 * its number");
#        define LINK_SET_E(n) ((int) offsetof(codegen_link_set_t, e[n]))

/*Entered from the link routine when the slot's key does not match, with X0 =
  X1 = the slot, W2 = cs + pc and X4 = pc | cs << 32. If an entry of the
  slot's set holds that key, it becomes the set's most recently used and the
  routine goes on with its checks on that entry's record as if the slot had
  matched. Otherwise, the block's own epilogue, as for any miss.*/
static void
build_link_set_routine(codeblock_t *block)
{
    static const void *const set_globals[] = { codegen_link_sets, codegen_link_set_mtf, codegen_link_table, codegen_link_slots, &codegen_link_epoch, &cpu_cur_status, &tsc, &tsc_old, &cycles_old, &timer_target, &codeblock, &codegen_link_cycles_min, &codegen_link_state_st, &codegen_link_state_cr };
    uint8_t                 *start_data;
    uint32_t                *no_set;
    uint32_t                *no_entry;
    uint32_t                *table_miss;
    uint32_t                *hint_hit;
    uint8_t                 *table_dest;
    uint8_t                 *miss_dest;
    uint8_t                 *rout;
    uint32_t                *table_short;

    codegen_alloc(block, BLOCK_MAX - 4);
    start_data = block_write_data;

    link_adrp = 1;
    for (size_t c = 0; c < sizeof(set_globals) / sizeof(set_globals[0]); c++) {
        if (!host_arm64_ADRP_in_range((uintptr_t) set_globals[c] - 0x1000)
            || !host_arm64_ADRP_in_range((uintptr_t) set_globals[c] + 0x1000))
            link_adrp = 0;
    }
    link_pool_nr = 0;

    rout = &block_write_data[block_pos];

    /*The table record the last table hop's record names as the one found
      after it (its hint): through a dispatch table the calls go round in
      the same order, and that record has been fetched. With the key, it is
      a table hit with no set to look through and no hash to work out*/
    {
        int offset = link_addr(block, REG_X6, &link_table_last);

        host_arm64_LDR_IMM_X(block, REG_X7, REG_X6, offset);
        host_arm64_LDRH_IMM(block, REG_W3, REG_X7, LINK_SLOT(hint));
        link_addr_exact(block, REG_X6, codegen_link_table);
        host_arm64_MOV_REG(block, REG_W5, REG_W3, 6);
        host_arm64_ADDX_REG_UXTW(block, REG_X5, REG_X6, REG_W5);
        host_arm64_LDP_IMM_X(block, REG_X6, REG_X15, REG_X5, LINK_SLOT(exit_pc));
        host_arm64_CMPX_REG(block, REG_X6, REG_X4);
        hint_hit = host_arm64_BEQ_SHORT_(block);
    }

    /*X5 = the set*/
    host_arm64_LDRH_IMM(block, REG_W3, REG_X0, LINK_SLOT(set));
    no_set = host_arm64_CBZ_SHORT_(block, REG_W3);
    link_addr_exact(block, REG_X5, codegen_link_sets);
    host_arm64_MOV_REG(block, REG_W3, REG_W3, 8);
    host_arm64_ADDX_REG_UXTW(block, REG_X5, REG_X5, REG_W3);

    /*X10 = the entry with the key, or 0; W11 = its number*/
    host_arm64_LDR_IMM_X(block, REG_X6, REG_X5, LINK_SET_E(0) + LINK_SLOT(exit_pc));
    host_arm64_LDR_IMM_X(block, REG_X7, REG_X5, LINK_SET_E(1) + LINK_SLOT(exit_pc));
    host_arm64_LDR_IMM_X(block, REG_X8, REG_X5, LINK_SET_E(2) + LINK_SLOT(exit_pc));
    host_arm64_ADDX_IMM(block, REG_X12, REG_X5, LINK_SET_E(0));
    host_arm64_ADDX_IMM(block, REG_X13, REG_X5, LINK_SET_E(1));
    host_arm64_ADDX_IMM(block, REG_X14, REG_X5, LINK_SET_E(2));
    host_arm64_MOVZ_IMM(block, REG_W16, 1);
    host_arm64_MOVZ_IMM(block, REG_W17, 2);
    host_arm64_CMPX_REG(block, REG_X6, REG_X4);
    host_arm64_CSELX_EQ(block, REG_X10, REG_X12, REG_XZR);
    host_arm64_CMPX_REG(block, REG_X7, REG_X4);
    host_arm64_CSELX_EQ(block, REG_X10, REG_X13, REG_X10);
    host_arm64_CSEL_EQ(block, REG_W11, REG_W16, REG_WZR);
    host_arm64_CMPX_REG(block, REG_X8, REG_X4);
    host_arm64_CSELX_EQ(block, REG_X10, REG_X14, REG_X10);
    host_arm64_CSEL_EQ(block, REG_W11, REG_W17, REG_W11);
    no_entry = host_arm64_CBZ_SHORT_(block, REG_X10);

    /*order = codegen_link_set_mtf[order][W11]*/
    host_arm64_LDRB_IMM_W(block, REG_W3, REG_X5, offsetof(codegen_link_set_t, order));
    link_addr_exact(block, REG_X6, codegen_link_set_mtf);
    host_arm64_ADD_REG(block, REG_W3, REG_W11, REG_W3, 2);
    host_arm64_LDRB_REG(block, REG_W3, REG_X6, REG_X3);
    host_arm64_STRB_IMM(block, REG_W3, REG_X5, offsetof(codegen_link_set_t, order));

    /*The routine goes on with the entry's record where it is, as with a
      table record: a site whose targets alternate would otherwise trade the
      two records, a line each, on every exit. X15 = its entry; W2 is still
      cs + pc*/
    host_arm64_LDR_IMM_X(block, REG_X15, REG_X10, LINK_SLOT(entry));
    host_arm64_MOVX_REG(block, REG_X1, REG_X10, 0);
    host_arm64_B(block, link_set_return);

    /*Not in a set: X5 = the table record at the key's hash, which the same
      checks go on with if it has the key (CODEGEN_LINK_TABLE_HASH)*/
    _Static_assert(sizeof(codegen_link_slot_t) == 64, "table records are indexed as 64 bytes");
    table_dest = &block_write_data[block_pos];
    host_arm64_LSR64_32(block, REG_X3, REG_X4);
    host_arm64_EOR_REG(block, REG_W3, REG_W3, REG_W4, 0);
    host_arm64_MOV_REG_LSR(block, REG_W5, REG_W3, 11);
    host_arm64_EOR_REG(block, REG_W3, REG_W3, REG_W5, 0);
    host_arm64_AND_IMM(block, REG_W3, REG_W3, CODEGEN_LINK_TABLE_SIZE - 1);
    host_arm64_MOV_REG(block, REG_W5, REG_W3, 6);
    link_addr_exact(block, REG_X6, codegen_link_table);
    host_arm64_ADDX_REG_UXTW(block, REG_X5, REG_X6, REG_W5);
    host_arm64_LDP_IMM_X(block, REG_X6, REG_X15, REG_X5, LINK_SLOT(exit_pc));
    host_arm64_CMPX_REG(block, REG_X6, REG_X4);
    table_miss = host_arm64_BNE_SHORT_(block);
    host_arm64_short_branch_set_offset(hint_hit, &block_write_data[block_pos]);
    /*W3 = its index. The records found before this one are told it: the
      last one as the record after it, the third last as the third after
      it. Through a dispatch table the calls go round in the same order:
      this one's third, fetched now, comes in over two hops, and its next
      one, fetched as a third two hops ago, is in, its block's code and
      codeblock_t fetched from it now*/
    int offset;

    link_addr_exact(block, REG_X6, link_table_last);
    host_arm64_LDP_IMM_X(block, REG_X7, REG_X9, REG_X6, 0);
    host_arm64_LDR_IMM_X(block, REG_X8, REG_X6, 16);
    host_arm64_STRH_IMM(block, REG_W3, REG_X7, LINK_SLOT(hint));
    host_arm64_STRH_IMM(block, REG_W3, REG_X8, LINK_SLOT(hint) + 2);
    host_arm64_STP_IMM_X(block, REG_X5, REG_X7, REG_X6, 0);
    host_arm64_STR_IMM_Q(block, REG_X9, REG_X6, 16);
    link_addr_exact(block, REG_X6, codegen_link_table);
    host_arm64_LDRH_IMM(block, REG_W8, REG_X5, LINK_SLOT(hint) + 2);
    host_arm64_MOV_REG(block, REG_W8, REG_W8, 6);
    host_arm64_ADDX_REG_UXTW(block, REG_X8, REG_X6, REG_W8);
    host_arm64_PRFM_IMM(block, REG_X8, 0);
    host_arm64_LDRH_IMM(block, REG_W7, REG_X5, LINK_SLOT(hint));
    host_arm64_MOV_REG(block, REG_W7, REG_W7, 6);
    host_arm64_ADDX_REG_UXTW(block, REG_X7, REG_X6, REG_W7);
    link_hint_prefetch(block);
    host_arm64_MOVX_REG(block, REG_X1, REG_X5, 0);
    /*A table record is filled for every exit that goes to its key, so it
      holds none of the bits that tell of the exit's block (two_page bits 1,
      2 and 5). The exit's own slot (X0) holds them for its block: one that
      calls no C and changes neither IF nor TF (bit 2), to a target without
      a static TOP, takes the short way as from its own slot*/
    {
        uint32_t *normal[3];

        host_arm64_LDRB_IMM_W(block, REG_W10, REG_X0, LINK_SLOT(two_page));
        normal[0] = host_arm64_TBZ_SHORT_(block, REG_W10, 2);
        host_arm64_LDRB_IMM_W(block, REG_W10, REG_X1, LINK_SLOT(top_mask));
        normal[1] = host_arm64_CBNZ_SHORT_(block, REG_W10);
        normal[2] = host_arm64_CBZ_SHORT_(block, REG_X15);
        table_short = host_arm64_B_(block);
        for (int c = 0; c < 3; c++)
            host_arm64_short_branch_set_offset(normal[c], &block_write_data[block_pos]);
    }
    host_arm64_B(block, link_set_return);

    miss_dest = &block_write_data[block_pos];
    host_arm64_B(block, codegen_link_miss_rout);

    /*The link routine's prefetch of the next block's code, then the target*/
    uint8_t *next_dest = &block_write_data[block_pos];
    {
        host_arm64_branch_set_offset(link_next_branch, &block_write_data[block_pos]);
        host_arm64_LDRH_IMM(block, REG_W4, REG_X1, LINK_SLOT(block_nr));
        host_arm64_LDRB_IMM_W(block, REG_W5, REG_X1, LINK_SLOT(end_slot));
        host_arm64_ADD_REG(block, REG_W4, REG_W4, REG_W4, 1);
        host_arm64_ADD_REG(block, REG_W4, REG_W4, REG_W5, 0);
        link_addr_exact(block, REG_X5, codegen_link_slots);
        host_arm64_ADDX_REG(block, REG_X5, REG_X5, REG_X4, 6);
        host_arm64_LDR_IMM_X(block, REG_X6, REG_X5, LINK_SLOT(entry));
        for (int c = 0; c < LINK_NEXT_CODE_LINES; c++)
            host_arm64_PRFM_I_IMM(block, REG_X6, c * 64);
        /*and its codeblock_t line, which the hop to it reads*/
        host_arm64_LDRH_IMM(block, REG_W7, REG_X5, LINK_SLOT(block_nr));
        offset = link_addr(block, REG_X8, &codeblock);
        host_arm64_LDR_IMM_X(block, REG_X8, REG_X8, offset);
        host_arm64_ADDX_REG(block, REG_X9, REG_X8, REG_X7, CODEBLOCK_SHIFT);
        host_arm64_PRFM_IMM(block, REG_X9, 0);
        /*and the slot of its end, which the next hop reads this from*/
        host_arm64_LDRB_IMM_W(block, REG_W8, REG_X5, LINK_SLOT(end_slot));
        host_arm64_ADD_REG(block, REG_W7, REG_W7, REG_W7, 1);
        host_arm64_ADD_REG(block, REG_W7, REG_W7, REG_W8, 0);
        link_addr_exact(block, REG_X9, codegen_link_slots);
        host_arm64_ADDX_REG(block, REG_X9, REG_X9, REG_X7, 6);
        host_arm64_PRFM_IMM(block, REG_X9, 0);
        host_arm64_BR(block, REG_X15);
    }

    /*The link routine's short way, for a record of an exit whose block calls
      no C and changes neither IF nor TF, to a target without a static TOP
      (two_page bit 5): with nothing having called C since the
      last full check (cpu_state.link_c_ran) and the record checked at this
      epoch, the events, the target's generation, its translation and
      mem_exec are as they were then. Its status, CR0 bits, dirty bits, the
      TSC and the timer are checked as the full way does, and it makes the
      same stores but those the full way only makes after a C call or on a
      new epoch. X1 = the record, X15 = its entry, W2 = cs + pc*/
    {
        uint32_t *normal[3];
        uint32_t *fmiss[12];
        uint32_t *prefetch;
        int       nf = 0;

        host_arm64_branch_set_offset(link_fast_branch, &block_write_data[block_pos]);
        host_arm64_branch_set_offset(table_short, &block_write_data[block_pos]);
        host_arm64_LDRB_IMM_W(block, REG_W10, REG_CPUSTATE, rep_state_offset(&cpu_state.link_c_ran));
        normal[0] = host_arm64_CBNZ_SHORT_(block, REG_W10);
        offset = link_addr(block, REG_X16, &codegen_link_epoch);
        host_arm64_LDR_IMM_W(block, REG_W16, REG_X16, offset);
        host_arm64_LDR_IMM_W(block, REG_W13, REG_X1, LINK_SLOT(epoch));
        host_arm64_CMP_REG(block, REG_W16, REG_W13);
        normal[1] = host_arm64_BNE_SHORT_(block);
        /*The target, X17, unchanged since the record was filled*/
        host_arm64_LDRH_IMM(block, REG_W17, REG_X1, LINK_SLOT(block_nr));
        offset = link_addr(block, REG_X4, &codeblock);
        host_arm64_LDR_IMM_X(block, REG_X4, REG_X4, offset);
        host_arm64_ADDX_REG(block, REG_X17, REG_X4, REG_X17, CODEBLOCK_SHIFT);
        host_arm64_LDP_IMM_W(block, REG_W5, REG_W6, REG_X1, LINK_SLOT(gen));
        host_arm64_LDR_IMM_W(block, REG_W4, REG_X17, offsetof(codeblock_t, link_gen));
        host_arm64_CMP_REG(block, REG_W4, REG_W5);
        fmiss[nf++] = host_arm64_BNE_SHORT_(block);
        /*Status and FPU rounding, as the full way compares them, with the
          state that check found (codegen_link_state_st): no C has run
          since, which an exit to here makes no call to*/
        /*and the paging and cache-disable bits and the CPL
          (codegen_link_state_cr), the two words as one: both are kept
          without the PC24 and RC_NZ bits*/
        host_arm64_LDR_IMM_W(block, REG_W9, REG_X1, LINK_SLOT(cr0_bits));
        host_arm64_ORRX_REG(block, REG_X6, REG_X6, REG_X9, 32);
        offset = link_addr(block, REG_X4, codegen_link_state);
        host_arm64_LDR_IMM_X(block, REG_X4, REG_X4, offset);
        host_arm64_CMPX_REG(block, REG_X4, REG_X6);
        fmiss[nf++] = host_arm64_BNE_SHORT_(block);
        /*Its code's dirty bits*/
        host_arm64_LDP_IMM_X(block, REG_X10, REG_X13, REG_X1, LINK_SLOT(page_mask));
        host_arm64_LDR_IMM_X(block, REG_X13, REG_X13, 0);
        host_arm64_TSTX_REG(block, REG_X13, REG_X10);
        fmiss[nf++] = host_arm64_BNE_SHORT_(block);
        {
            uint32_t *one_page;

            host_arm64_LDR_IMM_X(block, REG_X10, REG_X17, offsetof(codeblock_t, page_mask2));
            one_page = host_arm64_CBZ_SHORT_(block, REG_X10);
            host_arm64_LDR_IMM_X(block, REG_X13, REG_X17, offsetof(codeblock_t, dirty_mask2));
            host_arm64_LDR_IMM_X(block, REG_X13, REG_X13, 0);
            host_arm64_TSTX_REG(block, REG_X13, REG_X10);
            fmiss[nf++] = host_arm64_BNE_SHORT_(block);
            host_arm64_short_branch_set_offset(one_page, &block_write_data[block_pos]);
        }
        /*The TSC and the timer: with no C run since the full way's last
          check, neither the TSC nor timer_target has moved, and the C loop
          counts the cycles of every hop since then from cycles_old and
          tsc_old as they stand. The slice is not used up and no timer is
          due while cycles stays above what that check left
          (codegen_link_cycles_min); otherwise the full way works it out*/
        offset = link_addr(block, REG_X6, &codegen_link_cycles_min);
        host_arm64_LDR_IMM_W(block, REG_W7, REG_X6, offset);
        host_arm64_CMP_REG(block, REG_CYCLES, REG_W7);
        normal[2] = host_arm64_BLE_SHORT_(block);
        /*The target's used and runs_left (link_used_emit())*/
        link_used_emit(block);
        /*and the next block's code, as the full way prefetches it*/
        host_arm64_LDRB_IMM_W(block, REG_W4, REG_X1, LINK_SLOT(two_page));
        prefetch = host_arm64_TBZ_SHORT_(block, REG_W4, 3);
        host_arm64_B(block, next_dest);
        host_arm64_short_branch_set_offset(prefetch, &block_write_data[block_pos]);
        host_arm64_BR(block, REG_X15);
        for (int c = 0; c < 3; c++)
            host_arm64_short_branch_set_offset(normal[c], &block_write_data[block_pos]);
        host_arm64_B(block, link_fast_normal);
        for (int c = 0; c < nf; c++)
            host_arm64_short_branch_set_offset(fmiss[c], link_miss_dest);
    }

    /*The link routine's check of a target's static TOP, as when the slot was
      filled. X9 = block_nr | top_mask << 16 | top << 24 | two_page << 32*/
    {
        uint32_t *top_miss;

        host_arm64_short_branch_set_offset(link_top_check, &block_write_data[block_pos]);
        host_arm64_LDR_IMM_W(block, REG_W10, REG_CPUSTATE, rep_state_offset(&cpu_state.TOP));
        host_arm64_UBFX(block, REG_W13, REG_W9, 16, 8);
        host_arm64_AND_REG(block, REG_W10, REG_W10, REG_W13, 0);
        host_arm64_UBFX(block, REG_W13, REG_W9, 24, 8);
        host_arm64_CMP_REG(block, REG_W10, REG_W13);
        top_miss = host_arm64_BNE_SHORT_(block);
        host_arm64_B(block, link_top_done);
        host_arm64_short_branch_set_offset(top_miss, link_miss_dest);
    }

    /*The link routine's nmi set: miss if nmi_mask, or bit 1 (an NMI from the
      local APIC, not gated by port 70h), and nmi_enable too. W12 = nmi*/
    {
        uint32_t *nmi_ok;
        uint32_t *nmi_miss;

        host_arm64_short_branch_set_offset(link_nmi_set, &block_write_data[block_pos]);
        host_arm64_AND_IMM(block, REG_W3, REG_W12, 2);
        offset = link_addr(block, REG_X5, &nmi_mask);
        host_arm64_LDR_IMM_W(block, REG_W5, REG_X5, offset);
        host_arm64_ORR_REG(block, REG_W3, REG_W3, REG_W5, 0);
        nmi_ok = host_arm64_CBZ_SHORT_(block, REG_W3);
        host_arm64_short_branch_set_offset(nmi_ok, link_nmi_done);
        offset = link_addr(block, REG_X3, &nmi_enable);
        host_arm64_LDR_IMM_W(block, REG_W3, REG_X3, offset);
        nmi_miss = host_arm64_CBNZ_SHORT_(block, REG_W3);
        host_arm64_short_branch_set_offset(nmi_miss, link_miss_dest);
        host_arm64_B(block, link_nmi_done);
    }

    /*Its APIC path on: an ExtINT or an IRR vector at local APIC 0 is a miss*/
    {
        uint32_t *apic_miss;
        uint32_t *apic_miss_2;

        host_arm64_short_branch_set_offset(link_apic_set, &block_write_data[block_pos]);
        offset = link_addr(block, REG_X3, cpu_lapic_extint_live);
        host_arm64_LDRB_IMM_W(block, REG_W5, REG_X3, offset);
        apic_miss = host_arm64_CBNZ_SHORT_(block, REG_W5);
        host_arm64_short_branch_set_offset(apic_miss, link_miss_dest);
        offset = link_addr(block, REG_X3, &cpu_lapic_irr_live);
        host_arm64_LDR_IMM_W(block, REG_W5, REG_X3, offset);
        apic_miss_2 = host_arm64_TBNZ_SHORT_(block, REG_W5, 0);
        host_arm64_short_branch_set_offset(apic_miss_2, link_miss_dest);
        host_arm64_B(block, link_irq_done);
    }

    if (link_pool_nr)
        link_pool_emit();

    if (block_write_data != start_data)
        fatal("build_link_set_routine - routine does not fit in one allocator block\n");
    host_arm64_short_branch_set_offset(no_set, table_dest);
    host_arm64_short_branch_set_offset(no_entry, table_dest);
    host_arm64_short_branch_set_offset(table_miss, miss_dest);
    host_arm64_branch_set_offset(link_set_branch, rout);
}
#    endif

/*FLD m80 as x87_dyn_ld80() makes it (ropFLDe), called in its place: W0 =
  the segment number. A normal value in the double range, on a page the read
  lookup maps, is converted here as x87_from80() rounds it (to nearest
  even); anything else, and a run with breakpoints armed, goes to
  x87_dyn_ld80() itself*/
void *codegen_fld80_rout;

static const x86seg *const fld80_segs[6] = { &cpu_state.seg_es, &cpu_state.seg_cs, &cpu_state.seg_ss,
                                             &cpu_state.seg_ds, &cpu_state.seg_fs, &cpu_state.seg_gs };

static void
build_fld80_routine(codeblock_t *block)
{
    uint32_t *slow[8];
    uint32_t *no_carry;
    int       nr_slow = 0;

    codegen_alloc(block, 256);
    codegen_fld80_rout = &block_write_data[block_pos];
    /*cpu_state.ea_seg, as the C sets it first*/
    host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) fld80_segs);
    host_arm64_LDRX_REG_LSL3(block, REG_X2, REG_X1, REG_X0);
    host_arm64_STR_IMM_Q(block, REG_X2, REG_CPUSTATE, rep_state_offset(&cpu_state.ea_seg));
    /*W5 = easeg + eaaddr*/
    host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) &easeg);
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X1, 0);
    host_arm64_CMN_IMM(block, REG_W3, 1);
    slow[nr_slow++] = host_arm64_BEQ_SHORT_(block);
    host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) &cpu_dr_armed);
    host_arm64_LDR_IMM_W(block, REG_W4, REG_X1, 0);
    slow[nr_slow++] = host_arm64_CBNZ_SHORT_(block, REG_W4);
    host_arm64_LDR_IMM_W(block, REG_W5, REG_CPUSTATE, rep_state_offset(&cpu_state.eaaddr));
    host_arm64_ADD_REG(block, REG_W5, REG_W3, REG_W5, 0);
    /*The ten bytes on one page, which readlookup2[] maps*/
    host_arm64_AND_IMM(block, REG_W6, REG_W5, 0xfff);
    host_arm64_CMP_IMM(block, REG_W6, 0xff6);
    slow[nr_slow++] = host_arm64_BHI_SHORT_(block);
    host_arm64_MOV_REG_LSR(block, REG_W6, REG_W5, 12);
    host_arm64_LDRX_REG_LSL3(block, REG_X6, REG_LOOKUP, REG_X6);
    host_arm64_CMNX_IMM(block, REG_X6, 1);
    slow[nr_slow++] = host_arm64_BEQ_SHORT_(block);
    host_arm64_ADDX_REG_UXTW(block, REG_X6, REG_X6, REG_W5);
    host_arm64_LDR_IMM_X(block, REG_X7, REG_X6, 0);
    host_arm64_LDRH_IMM(block, REG_W8, REG_X6, 8);
    /*A normal significand (J set), the exponent a double's (1-2046 biased)*/
    slow[nr_slow++] = host_arm64_TBZ_SHORT_(block, REG_X7, 63);
    host_arm64_AND_IMM(block, REG_W9, REG_W8, 0x7fff);
    host_arm64_SUB_IMM(block, REG_W9, REG_W9, 16383 - 1023);
    host_arm64_SUB_IMM(block, REG_W10, REG_W9, 1);
    host_arm64_CMP_IMM(block, REG_W10, 2045);
    slow[nr_slow++] = host_arm64_BHI_SHORT_(block);
    /*X10 = the top 53 bits, rounded up when the 11 below are over half or
      half with X10 odd: (rest + 0x3ff + odd) >> 11*/
    host_arm64_UBFX_X(block, REG_X10, REG_X7, 11, 53);
    host_arm64_UBFX(block, REG_W11, REG_W7, 0, 11);
    host_arm64_UBFX(block, REG_W12, REG_W10, 0, 1);
    host_arm64_ADD_REG(block, REG_W11, REG_W11, REG_W12, 0);
    host_arm64_ADD_IMM(block, REG_W11, REG_W11, 0x3ff);
    host_arm64_MOV_REG_LSR(block, REG_W11, REG_W11, 11);
    host_arm64_ADDX_REG_UXTW(block, REG_X10, REG_X10, REG_W11);
    /*Rounded up to 2^53: the next exponent*/
    no_carry = host_arm64_TBZ_SHORT_(block, REG_X10, 53);
    host_arm64_UBFX_X(block, REG_X10, REG_X10, 1, 53);
    host_arm64_ADD_IMM(block, REG_W9, REG_W9, 1);
    host_arm64_CMP_IMM(block, REG_W9, 2046);
    slow[nr_slow++] = host_arm64_BHI_SHORT_(block);
    host_arm64_short_branch_set_offset(no_carry, &block_write_data[block_pos]);
    host_arm64_UBFX_X(block, REG_X10, REG_X10, 0, 52);
    host_arm64_ORRX_REG(block, REG_X10, REG_X10, REG_X9, 52);
    host_arm64_UBFX(block, REG_W13, REG_W8, 15, 1);
    host_arm64_ORRX_REG(block, REG_X10, REG_X10, REG_X13, 63);
    /*ST(-1) and its tag, as the push leaves them*/
    host_arm64_LDR_IMM_W(block, REG_W11, REG_CPUSTATE, rep_state_offset(&cpu_state.TOP));
    host_arm64_SUB_IMM(block, REG_W11, REG_W11, 1);
    host_arm64_AND_IMM(block, REG_W11, REG_W11, 7);
    host_arm64_ADDX_IMM(block, REG_X12, REG_CPUSTATE, rep_state_offset(&cpu_state.ST[0]));
    host_arm64_ADDX_REG(block, REG_X12, REG_X12, REG_X11, 3);
    host_arm64_STR_IMM_Q(block, REG_X10, REG_X12, 0);
    host_arm64_ADDX_IMM(block, REG_X12, REG_CPUSTATE, rep_state_offset(&cpu_state.tag[0]));
    host_arm64_MOVZ_IMM(block, REG_W13, TAG_VALID);
    host_arm64_STRB_REG(block, REG_W13, REG_X12, REG_X11);
    host_arm64_MOVZ_IMM(block, REG_W0, 0);
    host_arm64_RET(block, REG_X30);

    /*W0 is the segment number still*/
    for (int c = 0; c < nr_slow; c++)
        host_arm64_short_branch_set_offset(slow[c], &block_write_data[block_pos]);
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    host_arm64_call(block, (void *) x87_dyn_ld80);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);
}

/*FSTP m80 as x87_dyn_st80() makes it (ropFSTPe), likewise: a normal ST(0)
  to a page the write lookup maps (never one with code, whose writes are
  tracked) here, the rest by x87_dyn_st80()*/
void *codegen_fst80_rout;

static void
build_fst80_routine(codeblock_t *block)
{
    uint32_t *slow[8];
    int       nr_slow = 0;

    codegen_alloc(block, 256);
    codegen_fst80_rout = &block_write_data[block_pos];
    host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) fld80_segs);
    host_arm64_LDRX_REG_LSL3(block, REG_X2, REG_X1, REG_X0);
    host_arm64_STR_IMM_Q(block, REG_X2, REG_CPUSTATE, rep_state_offset(&cpu_state.ea_seg));
    host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) &easeg);
    host_arm64_LDR_IMM_W(block, REG_W3, REG_X1, 0);
    host_arm64_CMN_IMM(block, REG_W3, 1);
    slow[nr_slow++] = host_arm64_BEQ_SHORT_(block);
    host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) &cpu_dr_armed);
    host_arm64_LDR_IMM_W(block, REG_W4, REG_X1, 0);
    slow[nr_slow++] = host_arm64_CBNZ_SHORT_(block, REG_W4);
    host_arm64_LDR_IMM_W(block, REG_W5, REG_CPUSTATE, rep_state_offset(&cpu_state.eaaddr));
    host_arm64_ADD_REG(block, REG_W5, REG_W3, REG_W5, 0);
    host_arm64_AND_IMM(block, REG_W6, REG_W5, 0xfff);
    host_arm64_CMP_IMM(block, REG_W6, 0xff6);
    slow[nr_slow++] = host_arm64_BHI_SHORT_(block);
    host_arm64_MOV_REG_LSR(block, REG_W6, REG_W5, 12);
    host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) writelookup2);
    host_arm64_LDRX_REG_LSL3(block, REG_X6, REG_X1, REG_X6);
    host_arm64_CMNX_IMM(block, REG_X6, 1);
    slow[nr_slow++] = host_arm64_BEQ_SHORT_(block);
    /*X10 = ST(0), a normal double (biased exponent 1-2046)*/
    host_arm64_LDR_IMM_W(block, REG_W11, REG_CPUSTATE, rep_state_offset(&cpu_state.TOP));
    host_arm64_AND_IMM(block, REG_W11, REG_W11, 7);
    host_arm64_ADDX_IMM(block, REG_X12, REG_CPUSTATE, rep_state_offset(&cpu_state.ST[0]));
    host_arm64_ADDX_REG(block, REG_X12, REG_X12, REG_X11, 3);
    host_arm64_LDR_IMM_X(block, REG_X10, REG_X12, 0);
    host_arm64_UBFX_X(block, REG_X9, REG_X10, 52, 11);
    host_arm64_SUB_IMM(block, REG_W8, REG_W9, 1);
    host_arm64_CMP_IMM(block, REG_W8, 2045);
    slow[nr_slow++] = host_arm64_BHI_SHORT_(block);
    /*X11 = the significand with its integer bit, W13 = sign and exponent*/
    host_arm64_UBFX_X(block, REG_X11, REG_X10, 0, 52);
    host_arm64_ORRX_REG(block, REG_X11, REG_XZR, REG_X11, 11);
    host_arm64_MOVX_IMM(block, REG_X12, 0x8000000000000000ull);
    host_arm64_ORRX_REG(block, REG_X11, REG_X11, REG_X12, 0);
    host_arm64_ADD_IMM(block, REG_W13, REG_W9, 16383 - 1023);
    host_arm64_UBFX_X(block, REG_X14, REG_X10, 63, 1);
    host_arm64_ORR_REG(block, REG_W13, REG_W13, REG_W14, 15);
    host_arm64_ADDX_REG_UXTW(block, REG_X6, REG_X6, REG_W5);
    host_arm64_STR_IMM_Q(block, REG_X11, REG_X6, 0);
    host_arm64_STRH_IMM(block, REG_W13, REG_X6, 8);
    host_arm64_MOVZ_IMM(block, REG_W0, 0);
    host_arm64_RET(block, REG_X30);

    for (int c = 0; c < nr_slow; c++)
        host_arm64_short_branch_set_offset(slow[c], &block_write_data[block_pos]);
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    host_arm64_call(block, (void *) x87_dyn_st80);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);
}

#    ifdef CODEGEN_BACKEND_HAS_POLY
void *codegen_poly_tramp[CODEGEN_POLY_NR];

/*The trampolines of codegen_poly[], and the routine they go to. A
  trampoline is a block's start, entered as one is (codegen_enter_block()
  or linked): X1 = its entry and on. The routine compares the chunk, and
  the next one through its own mapping, with each version in turn, eight
  words of bytes a chunk under the version's mask, and
  goes to the link entry of the first that matches, which runs on as if
  linked to; with none, or a chunk's mapping changed, it sets miss and
  leaves to the C loop as at the block's start.*/
static void
build_poly_routines(codeblock_t *block)
{
    uint8_t  *rout;
    uint8_t  *loop;
    uint32_t *mapped;
    uint32_t *mapped_2;
    uint32_t *differ[16];
    uint32_t *one_chunk = NULL;
    uint32_t *more;
    uint32_t *none;

    _Static_assert(sizeof(codegen_poly_version_t) == 272, "versions are 272 bytes apart");

    codegen_alloc(block, BLOCK_MAX - 4);
    rout = &block_write_data[block_pos];
    /*X2 = the chunk, W4 = versions left, X5 = the version*/
    host_arm64_LDR_IMM_X(block, REG_X2, REG_X1, offsetof(codegen_poly_t, mem_exec));
    host_arm64_LDR_IMM_X(block, REG_X2, REG_X2, 0);
    host_arm64_LDR_IMM_X(block, REG_X3, REG_X1, offsetof(codegen_poly_t, exec));
    host_arm64_CMPX_REG(block, REG_X2, REG_X3);
    mapped = host_arm64_BNE_(block);
    host_arm64_LDR_IMM_X(block, REG_X6, REG_X1, offsetof(codegen_poly_t, mem_exec_2));
    host_arm64_LDR_IMM_X(block, REG_X6, REG_X6, 0);
    host_arm64_LDR_IMM_X(block, REG_X3, REG_X1, offsetof(codegen_poly_t, exec_2));
    host_arm64_CMPX_REG(block, REG_X6, REG_X3);
    mapped_2 = host_arm64_BNE_(block);
    /*X6 = 64 bytes before the next chunk*/
    host_arm64_LDR_IMM_X(block, REG_X6, REG_X1, offsetof(codegen_poly_t, code_2));
    host_arm64_LDR_IMM_X(block, REG_X2, REG_X1, offsetof(codegen_poly_t, code));
    host_arm64_LDR_IMM_W(block, REG_W4, REG_X1, offsetof(codegen_poly_t, nr));
    host_arm64_ADDX_IMM(block, REG_X5, REG_X1, offsetof(codegen_poly_t, v));
    loop = &block_write_data[block_pos];
    /*Word by word, a word with no code bytes passed over: most of a chunk
      is not the version's code. The first that differs goes on to the next
      version*/
    for (int w = 0; w < 16; w++) {
        uint32_t *skip;

        /*The next chunk only for a version with code there*/
        if (w == 8) {
            host_arm64_LDR_IMM_X(block, REG_X9, REG_X5, offsetof(codegen_poly_version_t, two));
            one_chunk = host_arm64_CBZ_SHORT_(block, REG_X9);
        }

        host_arm64_LDR_IMM_X(block, REG_X9, REG_X5, offsetof(codegen_poly_version_t, m) + w * 8);
        skip = host_arm64_CBZ_SHORT_(block, REG_X9);
        host_arm64_LDR_IMM_X(block, REG_X7, (w < 8) ? REG_X2 : REG_X6, w * 8);
        host_arm64_LDR_IMM_X(block, REG_X8, REG_X5, offsetof(codegen_poly_version_t, c) + w * 8);
        host_arm64_EORX_REG(block, REG_X7, REG_X7, REG_X8);
        host_arm64_TSTX_REG(block, REG_X7, REG_X9);
        differ[w] = host_arm64_BNE_SHORT_(block);
        host_arm64_short_branch_set_offset(skip, &block_write_data[block_pos]);
    }
    host_arm64_short_branch_set_offset(one_chunk, &block_write_data[block_pos]);
    host_arm64_LDR_IMM_X(block, REG_X15, REG_X5, offsetof(codegen_poly_version_t, entry));
    host_arm64_BR(block, REG_X15);
    for (int w = 0; w < 16; w++)
        host_arm64_short_branch_set_offset(differ[w], &block_write_data[block_pos]);
    host_arm64_ADDX_IMM(block, REG_X5, REG_X5, sizeof(codegen_poly_version_t));
    host_arm64_SUB_IMM(block, REG_W4, REG_W4, 1);
    host_arm64_CMP_IMM(block, REG_W4, 0);
    more = host_arm64_BNE_(block);
    host_arm64_branch_set_offset(more, loop);
    none = host_arm64_B_(block);
    host_arm64_branch_set_offset(mapped, &block_write_data[block_pos]);
    host_arm64_branch_set_offset(mapped_2, &block_write_data[block_pos]);
    host_arm64_branch_set_offset(none, &block_write_data[block_pos]);
    host_arm64_MOVZ_IMM(block, REG_W2, 1);
    host_arm64_STR_IMM_W(block, REG_W2, REG_X1, offsetof(codegen_poly_t, miss));
    host_arm64_B(block, codegen_exit_rout);

    for (int c = 0; c < CODEGEN_POLY_NR; c++) {
        codegen_alloc(block, 64);
        codegen_poly_tramp[c] = &block_write_data[block_pos];
        if ((uint8_t *) &block_write_data[block_pos] != ((uint8_t *) codegen_poly_tramp[c] + CODEGEN_LINK_ENTRY_OFFSET))
            fatal("build_poly_routines - link entry not where a block's is\n");
        host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) &codegen_poly[c]);
        host_arm64_B(block, rout);
    }
}
#    endif

static void
build_exit_charged(codeblock_t *block, void **entries, int nr, void *route)
{
    for (int c = 1; c <= nr; c++) {
        codegen_alloc(block, 16);
        entries[c - 1] = &block_write_data[block_pos];
        host_arm64_SUB_IMM(block, REG_CYCLES, REG_CYCLES, c);
        host_arm64_B(block, route);
    }
}

void *
codegen_exit_dest(void *route, int add)
{
    const int charge = -add;

    if (!add)
        return route;
    if (charge < 1)
        return NULL;
    if ((route == codegen_exit_rout) && (charge <= EXIT_CHARGED_EXIT))
        return codegen_exit_charged[charge - 1];
    if ((route == codegen_gpf_rout) && (charge <= EXIT_CHARGED_GPF))
        return codegen_gpf_charged[charge - 1];
    if ((route == codegen_ss_rout) && (charge <= EXIT_CHARGED_SS))
        return codegen_ss_charged[charge - 1];
    return NULL;
}

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
    host_arm64_STR_IMM_W(block, REG_CYCLES, REG_CPUSTATE, rep_state_offset(&cpu_state._cycles));
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

    build_exit_charged(block, codegen_exit_charged, EXIT_CHARGED_EXIT, codegen_exit_rout);
    build_exit_charged(block, codegen_gpf_charged, EXIT_CHARGED_GPF, codegen_gpf_rout);
    build_exit_charged(block, codegen_ss_charged, EXIT_CHARGED_SS, codegen_ss_rout);

    for (int is_stos = 0; is_stos < 2; is_stos++) {
        for (int shift = 0; shift < 3; shift++) {
            for (int a32 = 0; a32 < 2; a32++) {
                codegen_rep_rout[is_stos][shift][a32] = &block_write_data[block_pos];
                build_rep_routine(block, is_stos, shift, a32, codegen_rep_run[is_stos][shift][a32]);
            }
        }
    }

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    build_link_routine(block);
    build_link_set_routine(block);
#    endif
#    ifdef CODEGEN_BACKEND_HAS_POLY
    build_poly_routines(block);
#    endif
    build_fld80_routine(block);
    build_fst80_routine(block);

    /*The frame every exit unwinds (codegen_exit_rout), and X29 as blocks
      and the link routine expect it, then the block at X0*/
    codegen_alloc(block, 48);
    codegen_enter_block = (void *(*)(void *)) &block_write_data[block_pos];
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X27, REG_X28, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X25, REG_X26, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X23, REG_X24, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X21, REG_X22, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X19, REG_X20, REG_XSP, -64);
    host_arm64_MOVX_IMM(block, REG_CPUSTATE, (uint64_t) &cpu_state);
    host_arm64_MOVX_IMM(block, REG_LOOKUP, (uint64_t) (uintptr_t) readlookup2);
    host_arm64_LDR_IMM_W(block, REG_CYCLES, REG_CPUSTATE, rep_state_offset(&cpu_state._cycles));
    host_arm64_BR(block, REG_X0);

    block_write_data = NULL;

    codegen_allocator_clean_blocks(block->head_mem_block);

    {
        const void *veneer_targets[3 + EXIT_CHARGED_EXIT + EXIT_CHARGED_GPF + EXIT_CHARGED_SS];
        int         nr = 0;

        _Static_assert((sizeof(veneer_targets) / sizeof(veneer_targets[0])) <= (MEM_BLOCK_SIZE / 4),
                       "more veneer targets than a veneer block holds");
        veneer_targets[nr++] = codegen_exit_rout;
        veneer_targets[nr++] = codegen_gpf_rout;
        veneer_targets[nr++] = codegen_ss_rout;
        for (int c = 0; c < EXIT_CHARGED_EXIT; c++)
            veneer_targets[nr++] = codegen_exit_charged[c];
        for (int c = 0; c < EXIT_CHARGED_GPF; c++)
            veneer_targets[nr++] = codegen_gpf_charged[c];
        for (int c = 0; c < EXIT_CHARGED_SS; c++)
            veneer_targets[nr++] = codegen_ss_charged[c];
        codegen_allocator_set_veneers(veneer_targets, nr);
    }

    asm("mrs %0, fpcr\n"
        : "=r"(cpu_state.old_fp_control));
}

/*A block's slots compiled to one op (codegen_alu_family_carry[] 6), kept in
  its code memory after its start's miss path: the opcode of each, from
  base, and where its op and its start's check are, from the record. The
  check is a SUB of the opcode it was compiled for, the op one of the
  shifted-register forms*/
typedef struct family_fix_entry_t {
    uint16_t op_off;
    uint16_t pad;
    int32_t  alu; /*0 if the op was left out as dead*/
    int32_t  sub;
} family_fix_entry_t;
typedef struct family_fix_t {
    uint32_t           nr;
    /*The block's runs since the last rewrite, and rewrites in a row that
      came too soon after the one before to pay for themselves*/
    uint32_t           runs;
    uint32_t           quick;
    uint32_t           pad;
    const uint8_t     *base;
    family_fix_entry_t e[];
} family_fix_t;
static family_fix_t *family_fix_pending;
static uint8_t       family_fix_slot[CODEGEN_ALU_FAMILY_MAX];
extern uint32_t     *codegen_alu_family_fixed_at[CODEGEN_ALU_FAMILY_MAX];

static void
family_fix_write(uint32_t *insn, uint32_t value)
{
#    if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *))
        pthread_jit_write_protect_np(0);
#    endif
    *insn = value;
#    if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *))
        pthread_jit_write_protect_np(1);
#    endif
    __builtin___clear_cache((char *) insn, (char *) (insn + 1));
}

/*Called by the block's start when a slot's opcode is not the one it was
  compiled for: 0 once each such slot's op and check are rewritten for the
  opcode it holds now, 1 if one is not one of the five (the block is then
  compiled again)*/
int
codegen_family_fix(family_fix_t *d, codeblock_t *block)
{
    /*A rewrite costs about as much as five hundred slot runs saved:
      blocks rewritten sooner than that, again and again, read their opcodes
      as they run instead (compiled again, CODEBLOCK_FAMILY_NOFIX, and not
      retimed: codegen_alu_family_miss())*/
    if ((d->runs * d->nr) < 512) {
        if (++d->quick >= 3) {
            block->flags |= CODEBLOCK_FAMILY_NOFIX;
            codegen_family_nofix_block = block;
            return 1;
        }
    } else
        d->quick = 0;
    d->runs = 0;
    for (uint32_t c = 0; c < d->nr; c++) {
        const family_fix_entry_t *e   = &d->e[c];
        uint32_t                 *sub = (uint32_t *) ((uint8_t *) d + e->sub);
        const uint8_t             was = (*sub >> 10) & 0xfff;
        const uint8_t             now = d->base[e->op_off];
        uint32_t                  op;

        if (now == was)
            continue;
        switch (now) {
            case 0x05:
                op = 0x058u << 21; /*ADD*/
                break;
            case 0x0d:
                op = 0x150u << 21; /*ORR*/
                break;
            case 0x25:
                op = 0x050u << 21; /*AND*/
                break;
            case 0x2d:
                op = 0x258u << 21; /*SUB*/
                break;
            case 0x35:
                op = 0x250u << 21; /*EOR*/
                break;
            default:
                return 1;
        }
        if (e->alu) {
            uint32_t *alu = (uint32_t *) ((uint8_t *) d + e->alu);

            family_fix_write(alu, (*alu & 0x001fffff) | op);
        }
        family_fix_write(sub, (*sub & ~(0xfffu << 10)) | ((uint32_t) now << 10));
    }
    return 0;
}

/*At the start of a CODEBLOCK_ALU_FAMILY block, after the prologue so that a
  linked entry makes it too: every family opcode must still be one its
  instruction takes, or the block sets its family_miss and leaves before running
  anything. They are all on the block's first page.*/
void
codegen_backend_alu_family_check(codeblock_t *block)
{
    const uint8_t *base = codegen_alu_family_ptr[0];
    uint32_t      *ok;

    for (int c = 1; c < codegen_alu_family_nr; c++) {
        if (codegen_alu_family_ptr[c] < base)
            base = codegen_alu_family_ptr[c];
    }
    host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) base);
    /*A slot of kind 2-4 (codegen_alu_family_carry[]) checks its opcode in
      that kind's table*/
    static const int tables[5] = { -1, -1, REG_X7, REG_X8, REG_X9 };

    host_arm64_MOVX_IMM(block, REG_X7, (uint64_t) (uintptr_t) codegen_alu_family_bad_kind[0]);
    host_arm64_MOVX_IMM(block, REG_X8, (uint64_t) (uintptr_t) codegen_alu_family_bad_kind[1]);
    host_arm64_MOVX_IMM(block, REG_X9, (uint64_t) (uintptr_t) codegen_alu_family_bad_kind[2]);
    int retimed = 0;

    for (int c = 0; c < codegen_alu_family_nr; c++)
        retimed |= (codegen_alu_family_carry[c] == 5);
    if (retimed) {
        host_arm64_MOVX_IMM(block, REG_X10, (uint64_t) (uintptr_t) codegen_alu_family_kind);
        host_arm64_MOVZ_IMM(block, REG_W11, 0);
        host_arm64_MOVZ_IMM(block, REG_W12, 0);
    }
    int       nr_fixed = 0;
    uint32_t *fixed_sub[CODEGEN_ALU_FAMILY_MAX];

    host_arm64_MOVZ_IMM(block, REG_W3, 0);
    host_arm64_MOVZ_IMM(block, REG_W13, 0);
    for (int c = 0; c < codegen_alu_family_nr; c++) {
        codegen_alu_family_fixed_at[c] = NULL;
        host_arm64_LDRB_IMM_W(block, REG_W2, REG_X1, codegen_alu_family_ptr[c] - base);
        if (codegen_alu_family_carry[c] == 5) {
            /*A retimed slot: its kind into the key, 0x80 if it is none*/
            host_arm64_LDRB_REG(block, REG_W2, REG_X10, REG_X2);
            host_arm64_ORR_REG(block, REG_W12, REG_W12, REG_W2, 0);
            host_arm64_ORRX_REG(block, REG_X11, REG_X11, REG_X2, codegen_alu_family_key_shift[c]);
            continue;
        }
        if (codegen_alu_family_carry[c] == 6) {
            /*Fixed to one op: the exact opcode, its SUB rewritten with it*/
            codegen_alloc(block, 8);
            fixed_sub[nr_fixed] = (uint32_t *) &block_write_data[block_pos];
            family_fix_slot[nr_fixed++] = c;
            host_arm64_SUB_IMM(block, REG_W2, REG_W2, codegen_alu_family_op[c]);
            host_arm64_ORR_REG(block, REG_W13, REG_W13, REG_W2, 0);
            continue;
        }
        host_arm64_LDRB_REG(block, REG_W2, tables[codegen_alu_family_carry[c]], REG_X2);
        host_arm64_ORR_REG(block, REG_W3, REG_W3, REG_W2, 0);
    }
    if (retimed) {
        uint32_t *miss_1;
        uint32_t *miss_2;
        uint32_t *other;
        uint32_t *same;
        uint32_t *timed;

        /*The kinds the block was last timed for, by this compile: else the
          cycles they add are found (codegen_family_retime())*/
        host_arm64_ANDS_IMM(block, REG_W2, REG_W12, 0x80);
        host_arm64_ORR_REG(block, REG_W2, REG_W2, REG_W3, 0);
        host_arm64_CMP_IMM(block, REG_W2, 0);
        miss_1 = host_arm64_BNE_(block);
        host_arm64_MOVX_IMM(block, REG_X0, (uint64_t) (uintptr_t) block);
        host_arm64_LDR_IMM_X(block, REG_X2, REG_X0, offsetof(codeblock_t, family_key));
        host_arm64_CMPX_REG(block, REG_X11, REG_X2);
        other = host_arm64_BNE_(block);
        host_arm64_LDR_IMM_W(block, REG_W2, REG_X0, offsetof(codeblock_t, family_serial));
        host_arm64_MOVX_IMM(block, REG_X4, block->family_serial);
        host_arm64_CMP_REG_LSL(block, REG_W2, REG_W4, 0);
        same = host_arm64_BEQ_(block);
        host_arm64_branch_set_offset(other, &block_write_data[block_pos]);
        host_arm64_MOVX_REG(block, REG_X1, REG_X11, 0);
        host_arm64_MOVX_IMM(block, REG_X2, block->family_serial);
        host_arm64_call(block, codegen_family_retime);
        miss_2 = host_arm64_CBNZ_(block, REG_W0);
        timed  = host_arm64_B_(block);
        host_arm64_branch_set_offset(miss_1, &block_write_data[block_pos]);
        host_arm64_branch_set_offset(miss_2, &block_write_data[block_pos]);
        host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) &block->family_miss);
        host_arm64_MOVZ_IMM(block, REG_W2, 1);
        host_arm64_STRB_IMM(block, REG_W2, REG_X1, 0);
        host_arm64_B(block, codegen_exit_rout);
        host_arm64_branch_set_offset(same, &block_write_data[block_pos]);
        host_arm64_branch_set_offset(timed, &block_write_data[block_pos]);
        return;
    }
    if (nr_fixed)
        block->flags |= CODEBLOCK_FAMILY_FIXED_CODE;
    else
        block->flags &= ~CODEBLOCK_FAMILY_FIXED_CODE;
    if (nr_fixed) {
        uint32_t     *miss_1;
        uint32_t     *miss_2;
        uint32_t     *fixed_ok;
        uint32_t     *desc_ref;
        family_fix_t *d;

        /*The record's count of runs*/
        codegen_alloc(block, 20);
        desc_ref = (uint32_t *) &block_write_data[block_pos];
        host_arm64_MOVX_IMM(block, REG_X14, 0);
        host_arm64_MOVX_IMM(block, REG_X14, 0);
        host_arm64_LDR_IMM_W(block, REG_W2, REG_X14, offsetof(family_fix_t, runs));
        host_arm64_ADD_IMM(block, REG_W2, REG_W2, 1);
        host_arm64_STR_IMM_W(block, REG_W2, REG_X14, offsetof(family_fix_t, runs));
        /*Both clear: on. A fixed slot's opcode changed: rewritten, if it can be*/
        host_arm64_ORR_REG(block, REG_W2, REG_W3, REG_W13, 0);
        host_arm64_CMP_IMM(block, REG_W2, 0);
        ok     = host_arm64_BEQ_(block);
        miss_1 = host_arm64_CBNZ_(block, REG_W3);
        host_arm64_MOVX_REG(block, REG_X0, REG_X14, 0);
        host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) block);
        host_arm64_call(block, codegen_family_fix);
        miss_2   = host_arm64_CBNZ_(block, REG_W0);
        fixed_ok = host_arm64_B_(block);
        host_arm64_branch_set_offset(miss_1, &block_write_data[block_pos]);
        host_arm64_branch_set_offset(miss_2, &block_write_data[block_pos]);
        host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) &block->family_miss);
        host_arm64_MOVZ_IMM(block, REG_W2, 1);
        host_arm64_STRB_IMM(block, REG_W2, REG_X1, 0);
        host_arm64_B(block, codegen_exit_rout);
        /*The record, never run through*/
        codegen_alloc(block, sizeof(family_fix_t) + nr_fixed * sizeof(family_fix_entry_t) + 8);
        block_pos = (block_pos + 7) & ~7;
        d         = (family_fix_t *) &block_write_data[block_pos];
        d->nr     = nr_fixed;
        d->runs   = 0;
        d->quick  = 0;
        d->pad    = 0;
        d->base   = base;
        for (int c = 0; c < nr_fixed; c++) {
            d->e[c].op_off = codegen_alu_family_ptr[family_fix_slot[c]] - base;
            d->e[c].pad    = 0;
            d->e[c].alu    = 0;
            d->e[c].sub    = (int32_t) ((uint8_t *) fixed_sub[c] - (uint8_t *) d);
        }
        block_pos += sizeof(family_fix_t) + nr_fixed * sizeof(family_fix_entry_t);
        family_fix_pending = d;
        {
            /*X14 = the record, in the two moves reserved for it*/
            const int saved_pos = block_pos;
            uint8_t  *saved     = block_write_data;

            block_write_data = (uint8_t *) desc_ref;
            block_pos        = 0;
            host_arm64_MOVX_IMM(block, REG_X14, (uint64_t) (uintptr_t) d);
            if (block_pos > 8)
                fatal("codegen_backend_alu_family_check - the record is out of ADRP reach\n");
            while (block_pos < 8)
                host_arm64_NOP(block);
            block_write_data = saved;
            block_pos        = saved_pos;
        }
        host_arm64_branch_set_offset(ok, &block_write_data[block_pos]);
        host_arm64_branch_set_offset(fixed_ok, &block_write_data[block_pos]);
        return;
    }
    host_arm64_CMP_IMM(block, REG_W3, 0);
    ok = host_arm64_BEQ_(block);
    host_arm64_MOVX_IMM(block, REG_X1, (uint64_t) (uintptr_t) &block->family_miss);
    host_arm64_MOVZ_IMM(block, REG_W2, 1);
    host_arm64_STRB_IMM(block, REG_W2, REG_X1, 0);
    host_arm64_B(block, codegen_exit_rout);
    host_arm64_branch_set_offset(ok, &block_write_data[block_pos]);
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
    codegen_branch19_start();

    /*Entered with the registers saved and X29 set, from C through
      codegen_enter_block() and from the link routine*/

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    link_site            = 0;
    nr_link_direct_sites = 0;
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

/*An exit to a constant pc: the routine's second entry, which compares only
  the pc, since the block's slots were all filled with the CS it is keyed
  on, by its exits or its versions', so long as it calls no C, which could
  change CS. A block that does goes to the start after all
  (codegen_backend_epilogue())*/
void
codegen_link_exit_direct(codeblock_t *block)
{
    codegen_link_slot_t *slot;

    if (nr_link_direct_sites == LINK_DIRECT_SITES_MAX) {
        codegen_link_exit(block);
        return;
    }
    slot = &codegen_link_slots[get_block_nr(block)][link_site++ % CODEGEN_LINK_SLOTS];
    codegen_alloc(block, 20);
    host_arm64_MOVX_IMM(block, REG_X0, (uint64_t) (uintptr_t) slot);
    link_direct_sites[nr_link_direct_sites] = host_arm64_B_(block);
    host_arm64_branch_set_offset(link_direct_sites[nr_link_direct_sites++], codegen_link_rout_direct);
}
#    endif

void
codegen_backend_epilogue(codeblock_t *block)
{
#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    /*The fixed slots' ops are placed now*/
    if (family_fix_pending) {
        for (uint32_t c = 0; c < family_fix_pending->nr; c++) {
            const uint32_t *at = codegen_alu_family_fixed_at[family_fix_slot[c]];

            family_fix_pending->e[c].alu = at ? (int32_t) ((const uint8_t *) at - (const uint8_t *) family_fix_pending) : 0;
        }
        family_fix_pending = NULL;
    }
    if (codegen_block_link) {
        block->end_slot = link_site % CODEGEN_LINK_SLOTS;
        codegen_link_exit(block);
        codegen_lazy_stubs_emit(block);
        if (block->flags & CODEBLOCK_CALLS_C) {
            for (int c = 0; c < nr_link_direct_sites; c++)
                host_arm64_branch_set_offset(link_direct_sites[c], codegen_link_rout);
        }
        nr_link_direct_sites = 0;
        codegen_branch19_end();
        codegen_allocator_clean_blocks_used(block->head_mem_block, block_pos);
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

    codegen_lazy_stubs_emit(block);
    codegen_branch19_end();
    codegen_allocator_clean_blocks_used(block->head_mem_block, block_pos);
}

#endif
