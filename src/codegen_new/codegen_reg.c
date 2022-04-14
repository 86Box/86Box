#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "codegen.h"
#include "codegen_backend.h"
#include "codegen_ir_defs.h"
#include "codegen_reg.h"

int max_version_refcount;
uint16_t reg_dead_list = 0;

uint8_t reg_last_version[IREG_COUNT];
reg_version_t reg_version[IREG_COUNT][256];

ir_reg_t invalid_ir_reg = {IREG_INVALID};

ir_reg_t _host_regs[CODEGEN_HOST_REGS];
static uint8_t _host_reg_dirty[CODEGEN_HOST_REGS];

ir_reg_t host_fp_regs[CODEGEN_HOST_FP_REGS];
static uint8_t host_fp_reg_dirty[CODEGEN_HOST_FP_REGS];

typedef struct host_reg_set_t
{
        ir_reg_t *regs;
        uint8_t *dirty;
        host_reg_def_t *reg_list;
        uint16_t locked;
        int nr_regs;
} host_reg_set_t;

static host_reg_set_t host_reg_set, host_fp_reg_set;

enum
{
        REG_BYTE,
        REG_WORD,
        REG_DWORD,
        REG_QWORD,
        REG_POINTER,
        REG_DOUBLE,
        REG_FPU_ST_BYTE,
        REG_FPU_ST_DOUBLE,
        REG_FPU_ST_QWORD
};

enum
{
        REG_INTEGER,
        REG_FP
};

enum
{
        /*Register may be accessed outside of code block, and must be written
          back before any control transfers*/
        REG_PERMANENT = 0,
        /*Register will not be accessed outside of code block, and does not need
          to be written back if there are no readers remaining*/
        REG_VOLATILE = 1
};

struct
{
        int native_size;
        void *p;
        int type;
        int is_volatile;
} ireg_data[IREG_COUNT] =
{
        [IREG_EAX] = {REG_DWORD, &EAX, REG_INTEGER, REG_PERMANENT},
	[IREG_ECX] = {REG_DWORD, &ECX, REG_INTEGER, REG_PERMANENT},
	[IREG_EDX] = {REG_DWORD, &EDX, REG_INTEGER, REG_PERMANENT},
	[IREG_EBX] = {REG_DWORD, &EBX, REG_INTEGER, REG_PERMANENT},
	[IREG_ESP] = {REG_DWORD, &ESP, REG_INTEGER, REG_PERMANENT},
	[IREG_EBP] = {REG_DWORD, &EBP, REG_INTEGER, REG_PERMANENT},
	[IREG_ESI] = {REG_DWORD, &ESI, REG_INTEGER, REG_PERMANENT},
	[IREG_EDI] = {REG_DWORD, &EDI, REG_INTEGER, REG_PERMANENT},

	[IREG_flags_op]  = {REG_DWORD, &cpu_state.flags_op,  REG_INTEGER, REG_PERMANENT},
	[IREG_flags_res] = {REG_DWORD, &cpu_state.flags_res, REG_INTEGER, REG_PERMANENT},
	[IREG_flags_op1] = {REG_DWORD, &cpu_state.flags_op1, REG_INTEGER, REG_PERMANENT},
	[IREG_flags_op2] = {REG_DWORD, &cpu_state.flags_op2, REG_INTEGER, REG_PERMANENT},

	[IREG_pc]    = {REG_DWORD, &cpu_state.pc,    REG_INTEGER, REG_PERMANENT},
	[IREG_oldpc] = {REG_DWORD, &cpu_state.oldpc, REG_INTEGER, REG_PERMANENT},

	[IREG_eaaddr] = {REG_DWORD, &cpu_state.eaaddr,   REG_INTEGER, REG_PERMANENT},
	[IREG_ea_seg] = {REG_POINTER, &cpu_state.ea_seg, REG_INTEGER, REG_PERMANENT},

	[IREG_op32] = {REG_DWORD, &cpu_state.op32,  REG_INTEGER, REG_PERMANENT},
	[IREG_ssegsx] = {REG_BYTE, &cpu_state.ssegs, REG_INTEGER, REG_PERMANENT},

	[IREG_rm_mod_reg] = {REG_DWORD, &cpu_state.rm_data.rm_mod_reg_data, REG_INTEGER, REG_PERMANENT},

#ifdef USE_ACYCS
	[IREG_acycs]  = {REG_DWORD, &acycs, 		       REG_INTEGER, REG_PERMANENT},
#endif
	[IREG_cycles] = {REG_DWORD, &cpu_state._cycles,        REG_INTEGER, REG_PERMANENT},

	[IREG_CS_base] = {REG_DWORD, &cpu_state.seg_cs.base, REG_INTEGER, REG_PERMANENT},
	[IREG_DS_base] = {REG_DWORD, &cpu_state.seg_ds.base, REG_INTEGER, REG_PERMANENT},
	[IREG_ES_base] = {REG_DWORD, &cpu_state.seg_es.base, REG_INTEGER, REG_PERMANENT},
	[IREG_FS_base] = {REG_DWORD, &cpu_state.seg_fs.base, REG_INTEGER, REG_PERMANENT},
	[IREG_GS_base] = {REG_DWORD, &cpu_state.seg_gs.base, REG_INTEGER, REG_PERMANENT},
	[IREG_SS_base] = {REG_DWORD, &cpu_state.seg_ss.base, REG_INTEGER, REG_PERMANENT},

	[IREG_CS_seg] = {REG_WORD, &cpu_state.seg_cs.seg, REG_INTEGER, REG_PERMANENT},
	[IREG_DS_seg] = {REG_WORD, &cpu_state.seg_ds.seg, REG_INTEGER, REG_PERMANENT},
	[IREG_ES_seg] = {REG_WORD, &cpu_state.seg_es.seg, REG_INTEGER, REG_PERMANENT},
	[IREG_FS_seg] = {REG_WORD, &cpu_state.seg_fs.seg, REG_INTEGER, REG_PERMANENT},
	[IREG_GS_seg] = {REG_WORD, &cpu_state.seg_gs.seg, REG_INTEGER, REG_PERMANENT},
	[IREG_SS_seg] = {REG_WORD, &cpu_state.seg_ss.seg, REG_INTEGER, REG_PERMANENT},

	[IREG_FPU_TOP] = {REG_DWORD, &cpu_state.TOP, REG_INTEGER, REG_PERMANENT},

	[IREG_ST0] = {REG_FPU_ST_DOUBLE, &cpu_state.ST[0], REG_FP, REG_PERMANENT},
	[IREG_ST1] = {REG_FPU_ST_DOUBLE, &cpu_state.ST[0], REG_FP, REG_PERMANENT},
	[IREG_ST2] = {REG_FPU_ST_DOUBLE, &cpu_state.ST[0], REG_FP, REG_PERMANENT},
	[IREG_ST3] = {REG_FPU_ST_DOUBLE, &cpu_state.ST[0], REG_FP, REG_PERMANENT},
	[IREG_ST4] = {REG_FPU_ST_DOUBLE, &cpu_state.ST[0], REG_FP, REG_PERMANENT},
	[IREG_ST5] = {REG_FPU_ST_DOUBLE, &cpu_state.ST[0], REG_FP, REG_PERMANENT},
	[IREG_ST6] = {REG_FPU_ST_DOUBLE, &cpu_state.ST[0], REG_FP, REG_PERMANENT},
	[IREG_ST7] = {REG_FPU_ST_DOUBLE, &cpu_state.ST[0], REG_FP, REG_PERMANENT},

	[IREG_tag0] = {REG_FPU_ST_BYTE, &cpu_state.tag[0], REG_INTEGER, REG_PERMANENT},
	[IREG_tag1] = {REG_FPU_ST_BYTE, &cpu_state.tag[0], REG_INTEGER, REG_PERMANENT},
	[IREG_tag2] = {REG_FPU_ST_BYTE, &cpu_state.tag[0], REG_INTEGER, REG_PERMANENT},
	[IREG_tag3] = {REG_FPU_ST_BYTE, &cpu_state.tag[0], REG_INTEGER, REG_PERMANENT},
	[IREG_tag4] = {REG_FPU_ST_BYTE, &cpu_state.tag[0], REG_INTEGER, REG_PERMANENT},
	[IREG_tag5] = {REG_FPU_ST_BYTE, &cpu_state.tag[0], REG_INTEGER, REG_PERMANENT},
	[IREG_tag6] = {REG_FPU_ST_BYTE, &cpu_state.tag[0], REG_INTEGER, REG_PERMANENT},
	[IREG_tag7] = {REG_FPU_ST_BYTE, &cpu_state.tag[0], REG_INTEGER, REG_PERMANENT},

	[IREG_ST0_i64] = {REG_FPU_ST_QWORD, &cpu_state.MM[0], REG_FP, REG_PERMANENT},
	[IREG_ST1_i64] = {REG_FPU_ST_QWORD, &cpu_state.MM[0], REG_FP, REG_PERMANENT},
	[IREG_ST2_i64] = {REG_FPU_ST_QWORD, &cpu_state.MM[0], REG_FP, REG_PERMANENT},
	[IREG_ST3_i64] = {REG_FPU_ST_QWORD, &cpu_state.MM[0], REG_FP, REG_PERMANENT},
	[IREG_ST4_i64] = {REG_FPU_ST_QWORD, &cpu_state.MM[0], REG_FP, REG_PERMANENT},
	[IREG_ST5_i64] = {REG_FPU_ST_QWORD, &cpu_state.MM[0], REG_FP, REG_PERMANENT},
	[IREG_ST6_i64] = {REG_FPU_ST_QWORD, &cpu_state.MM[0], REG_FP, REG_PERMANENT},
	[IREG_ST7_i64] = {REG_FPU_ST_QWORD, &cpu_state.MM[0], REG_FP, REG_PERMANENT},

	[IREG_MM0x] = {REG_QWORD, &cpu_state.MM[0], REG_FP, REG_PERMANENT},
	[IREG_MM1x] = {REG_QWORD, &cpu_state.MM[1], REG_FP, REG_PERMANENT},
	[IREG_MM2x] = {REG_QWORD, &cpu_state.MM[2], REG_FP, REG_PERMANENT},
	[IREG_MM3x] = {REG_QWORD, &cpu_state.MM[3], REG_FP, REG_PERMANENT},
	[IREG_MM4x] = {REG_QWORD, &cpu_state.MM[4], REG_FP, REG_PERMANENT},
	[IREG_MM5x] = {REG_QWORD, &cpu_state.MM[5], REG_FP, REG_PERMANENT},
	[IREG_MM6x] = {REG_QWORD, &cpu_state.MM[6], REG_FP, REG_PERMANENT},
	[IREG_MM7x] = {REG_QWORD, &cpu_state.MM[7], REG_FP, REG_PERMANENT},

	[IREG_NPXCx] = {REG_WORD, &cpu_state.npxc, REG_INTEGER, REG_PERMANENT},
	[IREG_NPXSx] = {REG_WORD, &cpu_state.npxs, REG_INTEGER, REG_PERMANENT},

	[IREG_flagsx] = {REG_WORD, &cpu_state.flags, REG_INTEGER, REG_PERMANENT},
	[IREG_eflagsx] = {REG_WORD, &cpu_state.eflags, REG_INTEGER, REG_PERMANENT},

	[IREG_CS_limit_low] = {REG_DWORD, &cpu_state.seg_cs.limit_low, REG_INTEGER, REG_PERMANENT},
	[IREG_DS_limit_low] = {REG_DWORD, &cpu_state.seg_ds.limit_low, REG_INTEGER, REG_PERMANENT},
	[IREG_ES_limit_low] = {REG_DWORD, &cpu_state.seg_es.limit_low, REG_INTEGER, REG_PERMANENT},
	[IREG_FS_limit_low] = {REG_DWORD, &cpu_state.seg_fs.limit_low, REG_INTEGER, REG_PERMANENT},
	[IREG_GS_limit_low] = {REG_DWORD, &cpu_state.seg_gs.limit_low, REG_INTEGER, REG_PERMANENT},
	[IREG_SS_limit_low] = {REG_DWORD, &cpu_state.seg_ss.limit_low, REG_INTEGER, REG_PERMANENT},

	[IREG_CS_limit_high] = {REG_DWORD, &cpu_state.seg_cs.limit_high, REG_INTEGER, REG_PERMANENT},
	[IREG_DS_limit_high] = {REG_DWORD, &cpu_state.seg_ds.limit_high, REG_INTEGER, REG_PERMANENT},
	[IREG_ES_limit_high] = {REG_DWORD, &cpu_state.seg_es.limit_high, REG_INTEGER, REG_PERMANENT},
	[IREG_FS_limit_high] = {REG_DWORD, &cpu_state.seg_fs.limit_high, REG_INTEGER, REG_PERMANENT},
	[IREG_GS_limit_high] = {REG_DWORD, &cpu_state.seg_gs.limit_high, REG_INTEGER, REG_PERMANENT},
	[IREG_SS_limit_high] = {REG_DWORD, &cpu_state.seg_ss.limit_high, REG_INTEGER, REG_PERMANENT},

	/*Temporary registers are stored on the stack, and are not guaranteed to
          be preserved across uOPs. They will not be written back if they will
          not be read again.*/
	[IREG_temp0] = {REG_DWORD, (void *)16, REG_INTEGER, REG_VOLATILE},
	[IREG_temp1] = {REG_DWORD, (void *)20, REG_INTEGER, REG_VOLATILE},
	[IREG_temp2] = {REG_DWORD, (void *)24, REG_INTEGER, REG_VOLATILE},
	[IREG_temp3] = {REG_DWORD, (void *)28, REG_INTEGER, REG_VOLATILE},

	[IREG_temp0d] = {REG_DOUBLE, (void *)40, REG_FP, REG_VOLATILE},
	[IREG_temp1d] = {REG_DOUBLE, (void *)48, REG_FP, REG_VOLATILE},
};

void codegen_reg_mark_as_required()
{
        int reg;

        for (reg = 0; reg < IREG_COUNT; reg++)
        {
                int last_version = reg_last_version[reg];

                if (last_version > 0 && ireg_data[reg].is_volatile == REG_PERMANENT)
                        reg_version[reg][last_version].flags |= REG_FLAGS_REQUIRED;
        }
}

int reg_is_native_size(ir_reg_t ir_reg)
{
        int native_size = ireg_data[IREG_GET_REG(ir_reg.reg)].native_size;
        int requested_size = IREG_GET_SIZE(ir_reg.reg);

        switch (native_size)
        {
                case REG_BYTE: case REG_FPU_ST_BYTE:
                return (requested_size == IREG_SIZE_B);
                case REG_WORD:
                return (requested_size == IREG_SIZE_W);
                case REG_DWORD:
                return (requested_size == IREG_SIZE_L);
                case REG_QWORD: case REG_FPU_ST_QWORD: case REG_DOUBLE: case REG_FPU_ST_DOUBLE:
                return ((requested_size == IREG_SIZE_D) || (requested_size == IREG_SIZE_Q));
                case REG_POINTER:
                if (sizeof(void *) == 4)
                        return (requested_size == IREG_SIZE_L);
                return (requested_size == IREG_SIZE_Q);

                default:
                fatal("get_reg_is_native_size: unknown native size %i\n", native_size);
        }

        return 0;
}

void codegen_reg_reset()
{
        int c;

        host_reg_set.regs = _host_regs;
        host_reg_set.dirty = _host_reg_dirty;
        host_reg_set.reg_list = codegen_host_reg_list;
        host_reg_set.locked = 0;
        host_reg_set.nr_regs = CODEGEN_HOST_REGS;
        host_fp_reg_set.regs = host_fp_regs;
        host_fp_reg_set.dirty = host_fp_reg_dirty;
        host_fp_reg_set.reg_list = codegen_host_fp_reg_list;
        host_fp_reg_set.locked = 0;
        host_fp_reg_set.nr_regs = CODEGEN_HOST_FP_REGS;

        for (c = 0; c < IREG_COUNT; c++)
        {
                reg_last_version[c] = 0;
                reg_version[c][0].refcount = 0;
        }
        for (c = 0; c < CODEGEN_HOST_REGS; c++)
        {
                host_reg_set.regs[c] = invalid_ir_reg;
                host_reg_set.dirty[c] = 0;
        }
        for (c = 0; c < CODEGEN_HOST_FP_REGS; c++)
        {
                host_fp_reg_set.regs[c] = invalid_ir_reg;
                host_fp_reg_set.dirty[c] = 0;
        }

        reg_dead_list = 0;
        max_version_refcount = 0;
}

static inline int ir_get_refcount(ir_reg_t ir_reg)
{
        return reg_version[IREG_GET_REG(ir_reg.reg)][ir_reg.version].refcount;
}

static inline host_reg_set_t *get_reg_set(ir_reg_t ir_reg)
{
        if (ireg_data[IREG_GET_REG(ir_reg.reg)].type == REG_INTEGER)
                return &host_reg_set;
        else
                return &host_fp_reg_set;
}

static void codegen_reg_load(host_reg_set_t *reg_set, codeblock_t *block, int c, ir_reg_t ir_reg)
{
        switch (ireg_data[IREG_GET_REG(ir_reg.reg)].native_size)
        {
                case REG_WORD:
#ifndef RELEASE_BUILD
                if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_INTEGER)
                        fatal("codegen_reg_load - REG_WORD !REG_INTEGER\n");
#endif
                if ((uintptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                        codegen_direct_read_16_stack(block, reg_set->reg_list[c].reg, (intptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                else
                        codegen_direct_read_16(block, reg_set->reg_list[c].reg, ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                break;

                case REG_DWORD:
#ifndef RELEASE_BUILD
                if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_INTEGER)
                        fatal("codegen_reg_load - REG_DWORD !REG_INTEGER\n");
#endif
                if ((uintptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                        codegen_direct_read_32_stack(block, reg_set->reg_list[c].reg, (intptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                else
                        codegen_direct_read_32(block, reg_set->reg_list[c].reg, ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                break;

                case REG_QWORD:
#ifndef RELEASE_BUILD
                if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_FP)
                        fatal("codegen_reg_load - REG_QWORD !REG_FP\n");
#endif
                if ((uintptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                        codegen_direct_read_64_stack(block, reg_set->reg_list[c].reg, (intptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                else
                        codegen_direct_read_64(block, reg_set->reg_list[c].reg, ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                break;

                case REG_POINTER:
#ifndef RELEASE_BUILD
                if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_INTEGER)
                        fatal("codegen_reg_load - REG_POINTER !REG_INTEGER\n");
#endif
                if ((uintptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                        codegen_direct_read_pointer_stack(block, reg_set->reg_list[c].reg, (intptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                else
                        codegen_direct_read_pointer(block, reg_set->reg_list[c].reg, ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                break;

                case REG_DOUBLE:
#ifndef RELEASE_BUILD
                if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_FP)
                        fatal("codegen_reg_load - REG_DOUBLE !REG_FP\n");
#endif
                if ((uintptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                        codegen_direct_read_double_stack(block, reg_set->reg_list[c].reg, (intptr_t)ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                else
                        codegen_direct_read_double(block, reg_set->reg_list[c].reg, ireg_data[IREG_GET_REG(ir_reg.reg)].p);
                break;

                case REG_FPU_ST_BYTE:
#ifndef RELEASE_BUILD
                if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_INTEGER)
                        fatal("codegen_reg_load - REG_FPU_ST_BYTE !REG_INTEGER\n");
#endif
                if (block->flags & CODEBLOCK_STATIC_TOP)
                        codegen_direct_read_8(block, reg_set->reg_list[c].reg, &cpu_state.tag[ir_reg.reg & 7]);
                else
                        codegen_direct_read_st_8(block, reg_set->reg_list[c].reg, &cpu_state.tag[0], ir_reg.reg & 7);
                break;

                case REG_FPU_ST_QWORD:
#ifndef RELEASE_BUILD
                if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_FP)
                        fatal("codegen_reg_load - REG_FPU_ST_QWORD !REG_FP\n");
#endif
                if (block->flags & CODEBLOCK_STATIC_TOP)
                        codegen_direct_read_64(block, reg_set->reg_list[c].reg, &cpu_state.MM[ir_reg.reg & 7]);
                else
                        codegen_direct_read_st_64(block, reg_set->reg_list[c].reg, &cpu_state.MM[0], ir_reg.reg & 7);
                break;

                case REG_FPU_ST_DOUBLE:
#ifndef RELEASE_BUILD
                if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_FP)
                        fatal("codegen_reg_load - REG_FPU_ST_DOUBLE !REG_FP\n");
#endif
                if (block->flags & CODEBLOCK_STATIC_TOP)
                        codegen_direct_read_double(block, reg_set->reg_list[c].reg, &cpu_state.ST[ir_reg.reg & 7]);
                else
                        codegen_direct_read_st_double(block, reg_set->reg_list[c].reg, &cpu_state.ST[0], ir_reg.reg & 7);
                break;

                default:
                fatal("codegen_reg_load - native_size=%i reg=%i\n", ireg_data[IREG_GET_REG(ir_reg.reg)].native_size, IREG_GET_REG(ir_reg.reg));
        }

        reg_set->regs[c] = ir_reg;
}

static void codegen_reg_writeback(host_reg_set_t *reg_set, codeblock_t *block, int c, int invalidate)
{
        int ir_reg = IREG_GET_REG(reg_set->regs[c].reg);
        void *p = ireg_data[ir_reg].p;

        if (!reg_version[ir_reg][reg_set->regs[c].version].refcount &&
                        ireg_data[ir_reg].is_volatile)
                return;

        switch (ireg_data[ir_reg].native_size)
        {
                case REG_BYTE:
#ifndef RELEASE_BUILD
                if (ireg_data[ir_reg].type != REG_INTEGER)
                        fatal("codegen_reg_writeback - REG_BYTE !REG_INTEGER\n");
                if ((uintptr_t)p < 256)
                        fatal("codegen_reg_writeback - REG_BYTE %p\n", p);
#endif
                codegen_direct_write_8(block, p, reg_set->reg_list[c].reg);
                break;

                case REG_WORD:
#ifndef RELEASE_BUILD
                if (ireg_data[ir_reg].type != REG_INTEGER)
                        fatal("codegen_reg_writeback - REG_WORD !REG_INTEGER\n");
                if ((uintptr_t)p < 256)
                        fatal("codegen_reg_writeback - REG_WORD %p\n", p);
#endif
                codegen_direct_write_16(block, p, reg_set->reg_list[c].reg);
                break;

                case REG_DWORD:
#ifndef RELEASE_BUILD
                if (ireg_data[ir_reg].type != REG_INTEGER)
                        fatal("codegen_reg_writeback - REG_DWORD !REG_INTEGER\n");
#endif
                if ((uintptr_t)p < 256)
                        codegen_direct_write_32_stack(block, (intptr_t)p, reg_set->reg_list[c].reg);
                else
                        codegen_direct_write_32(block, p, reg_set->reg_list[c].reg);
                break;

                case REG_QWORD:
#ifndef RELEASE_BUILD
                if (ireg_data[ir_reg].type != REG_FP)
                        fatal("codegen_reg_writeback - REG_QWORD !REG_FP\n");
#endif
                if ((uintptr_t)p < 256)
                        codegen_direct_write_64_stack(block, (intptr_t)p, reg_set->reg_list[c].reg);
                else
                        codegen_direct_write_64(block, p, reg_set->reg_list[c].reg);
                break;

                case REG_POINTER:
#ifndef RELEASE_BUILD
                if (ireg_data[ir_reg].type != REG_INTEGER)
                        fatal("codegen_reg_writeback - REG_POINTER !REG_INTEGER\n");
                if ((uintptr_t)p < 256)
                        fatal("codegen_reg_writeback - REG_POINTER %p\n", p);
#endif
                codegen_direct_write_ptr(block, p, reg_set->reg_list[c].reg);
                break;

                case REG_DOUBLE:
#ifndef RELEASE_BUILD
                if (ireg_data[ir_reg].type != REG_FP)
                        fatal("codegen_reg_writeback - REG_DOUBLE !REG_FP\n");
#endif
                if ((uintptr_t)p < 256)
                        codegen_direct_write_double_stack(block, (intptr_t)p, reg_set->reg_list[c].reg);
                else
                        codegen_direct_write_double(block, p, reg_set->reg_list[c].reg);
                break;

                case REG_FPU_ST_BYTE:
#ifndef RELEASE_BUILD
                if (ireg_data[ir_reg].type != REG_INTEGER)
                        fatal("codegen_reg_writeback - REG_FPU_ST_BYTE !REG_INTEGER\n");
#endif
                if (block->flags & CODEBLOCK_STATIC_TOP)
                        codegen_direct_write_8(block, &cpu_state.tag[reg_set->regs[c].reg & 7], reg_set->reg_list[c].reg);
                else
                        codegen_direct_write_st_8(block, &cpu_state.tag[0], reg_set->regs[c].reg & 7, reg_set->reg_list[c].reg);
                break;

                case REG_FPU_ST_QWORD:
#ifndef RELEASE_BUILD
                if (ireg_data[ir_reg].type != REG_FP)
                        fatal("codegen_reg_writeback - REG_FPU_ST_QWORD !REG_FP\n");
#endif
                if (block->flags & CODEBLOCK_STATIC_TOP)
                        codegen_direct_write_64(block, &cpu_state.MM[reg_set->regs[c].reg & 7], reg_set->reg_list[c].reg);
                else
                        codegen_direct_write_st_64(block, &cpu_state.MM[0], reg_set->regs[c].reg & 7, reg_set->reg_list[c].reg);
                break;

                case REG_FPU_ST_DOUBLE:
#ifndef RELEASE_BUILD
                if (ireg_data[ir_reg].type != REG_FP)
                        fatal("codegen_reg_writeback - REG_FPU_ST_DOUBLE !REG_FP\n");
#endif
                if (block->flags & CODEBLOCK_STATIC_TOP)
                        codegen_direct_write_double(block, &cpu_state.ST[reg_set->regs[c].reg & 7], reg_set->reg_list[c].reg);
                else
                        codegen_direct_write_st_double(block, &cpu_state.ST[0], reg_set->regs[c].reg & 7, reg_set->reg_list[c].reg);
                break;

                default:
                fatal("codegen_reg_flush - native_size=%i\n", ireg_data[ir_reg].native_size);
        }

        if (invalidate)
                reg_set->regs[c] = invalid_ir_reg;
        reg_set->dirty[c] = 0;
}

#ifdef CODEGEN_BACKEND_HAS_MOV_IMM
void codegen_reg_write_imm(codeblock_t *block, ir_reg_t ir_reg, uint32_t imm_data)
{
        int reg_idx = IREG_GET_REG(ir_reg.reg);
        void *p = ireg_data[reg_idx].p;

        switch (ireg_data[reg_idx].native_size)
        {
                case REG_BYTE:
#ifndef RELEASE_BUILD
                if ((uintptr_t)p < 256)
                        fatal("codegen_reg_write_imm - REG_BYTE %p\n", p);
#endif
                codegen_direct_write_8_imm(block, p, imm_data);
                break;

                case REG_WORD:
#ifndef RELEASE_BUILD
                if ((uintptr_t)p < 256)
                        fatal("codegen_reg_write_imm - REG_WORD %p\n", p);
#endif
                codegen_direct_write_16_imm(block, p, imm_data);
                break;

                case REG_DWORD:
                if ((uintptr_t)p < 256)
                        codegen_direct_write_32_imm_stack(block, (int)((uintptr_t) p), imm_data);
                else
                        codegen_direct_write_32_imm(block, p, imm_data);
                break;

                case REG_POINTER:
                case REG_QWORD:
                case REG_DOUBLE:
                case REG_FPU_ST_BYTE:
                case REG_FPU_ST_QWORD:
                case REG_FPU_ST_DOUBLE:
                default:
                fatal("codegen_reg_write_imm - native_size=%i\n", ireg_data[reg_idx].native_size);
        }
}
#endif

static void alloc_reg(ir_reg_t ir_reg)
{
        host_reg_set_t *reg_set = get_reg_set(ir_reg);
        int nr_regs = (reg_set == &host_reg_set) ? CODEGEN_HOST_REGS : CODEGEN_HOST_FP_REGS;
        int c;

        for (c = 0; c < nr_regs; c++)
        {
                if (IREG_GET_REG(reg_set->regs[c].reg) == IREG_GET_REG(ir_reg.reg))
                {
#ifndef RELEASE_BUILD
                        if (reg_set->regs[c].version != ir_reg.version)
                                fatal("alloc_reg - host_regs[c].version != ir_reg.version  %i %p %p  %i %i\n", c, reg_set, &host_reg_set, reg_set->regs[c].reg, ir_reg.reg);
#endif
                        reg_set->locked |= (1 << c);
                        return;
                }
        }
}

static void alloc_dest_reg(ir_reg_t ir_reg, int dest_reference)
{
        host_reg_set_t *reg_set = get_reg_set(ir_reg);
        int nr_regs = (reg_set == &host_reg_set) ? CODEGEN_HOST_REGS : CODEGEN_HOST_FP_REGS;
        int c;

        for (c = 0; c < nr_regs; c++)
        {
                if (IREG_GET_REG(reg_set->regs[c].reg) == IREG_GET_REG(ir_reg.reg))
                {
                        if (reg_set->regs[c].version == ir_reg.version)
                        {
                                reg_set->locked |= (1 << c);
                        }
                        else
                        {
                                /*The immediate prior version may have been
                                  optimised out, so search backwards to find the
                                  last valid version*/
                                int prev_version = ir_reg.version-1;
                                while (prev_version >= 0)
                                {
                                        reg_version_t *regv = &reg_version[IREG_GET_REG(reg_set->regs[c].reg)][prev_version];

                                        if (!(regv->flags & REG_FLAGS_DEAD) && regv->refcount == dest_reference)
                                        {
                                                reg_set->locked |= (1 << c);
                                                return;
                                        }
                                        prev_version--;
                                }
                                fatal("codegen_reg_alloc_register - host_regs[c].version != dest_reg_a.version  %i,%i %i\n", reg_set->regs[c].version, ir_reg.version, dest_reference);
                        }
                        return;
                }
        }
}

void codegen_reg_alloc_register(ir_reg_t dest_reg_a, ir_reg_t src_reg_a, ir_reg_t src_reg_b, ir_reg_t src_reg_c)
{
        int dest_reference = 0;

        host_reg_set.locked = 0;
        host_fp_reg_set.locked = 0;

        if (!ir_reg_is_invalid(dest_reg_a))
        {
                if (!ir_reg_is_invalid(src_reg_a) && IREG_GET_REG(src_reg_a.reg) == IREG_GET_REG(dest_reg_a.reg) && src_reg_a.version == dest_reg_a.version-1)
                        dest_reference++;
                if (!ir_reg_is_invalid(src_reg_b) && IREG_GET_REG(src_reg_b.reg) == IREG_GET_REG(dest_reg_a.reg) && src_reg_b.version == dest_reg_a.version-1)
                        dest_reference++;
                if (!ir_reg_is_invalid(src_reg_c) && IREG_GET_REG(src_reg_c.reg) == IREG_GET_REG(dest_reg_a.reg) && src_reg_c.version == dest_reg_a.version-1)
                        dest_reference++;
        }
        if (!ir_reg_is_invalid(src_reg_a))
                alloc_reg(src_reg_a);
        if (!ir_reg_is_invalid(src_reg_b))
                alloc_reg(src_reg_b);
        if (!ir_reg_is_invalid(src_reg_c))
                alloc_reg(src_reg_c);
        if (!ir_reg_is_invalid(dest_reg_a))
                alloc_dest_reg(dest_reg_a, dest_reference);
}

ir_host_reg_t codegen_reg_alloc_read_reg(codeblock_t *block, ir_reg_t ir_reg, int *host_reg_idx)
{
        host_reg_set_t *reg_set = get_reg_set(ir_reg);
        int c;

        /*Search for required register*/
        for (c = 0; c < reg_set->nr_regs; c++)
        {
                if (!ir_reg_is_invalid(reg_set->regs[c]) && IREG_GET_REG(reg_set->regs[c].reg) == IREG_GET_REG(ir_reg.reg) && reg_set->regs[c].version == ir_reg.version)
                        break;

                if (!ir_reg_is_invalid(reg_set->regs[c]) && IREG_GET_REG(reg_set->regs[c].reg) == IREG_GET_REG(ir_reg.reg) && reg_set->regs[c].version <= ir_reg.version)
                {
                        reg_version[IREG_GET_REG(reg_set->regs[c].reg)][reg_set->regs[c].version].refcount++;
                        break;
                }

#ifndef RELEASE_BUILD
                if (!ir_reg_is_invalid(reg_set->regs[c]) && IREG_GET_REG(reg_set->regs[c].reg) == IREG_GET_REG(ir_reg.reg) && reg_version[IREG_GET_REG(reg_set->regs[c].reg)][reg_set->regs[c].version].refcount)
                        fatal("codegen_reg_alloc_read_reg - version mismatch!\n");
#endif
        }

        if (c == reg_set->nr_regs)
        {
                /*No unused registers. Search for an unlocked register with no pending reads*/
                for (c = 0; c < reg_set->nr_regs; c++)
                {
                        if (!(reg_set->locked & (1 << c)) && IREG_GET_REG(reg_set->regs[c].reg) != IREG_INVALID && !ir_get_refcount(reg_set->regs[c]))
                                break;
                }
                if (c == reg_set->nr_regs)
                {
                        /*Search for any unlocked register*/
                        for (c = 0; c < reg_set->nr_regs; c++)
                        {
                                if (!(reg_set->locked & (1 << c)))
                                        break;
                        }
#ifndef RELEASE_BUILD
                        if (c == reg_set->nr_regs)
                                fatal("codegen_reg_alloc_read_reg - out of registers\n");
#endif
                }
                if (reg_set->dirty[c])
                        codegen_reg_writeback(reg_set, block, c, 1);
                codegen_reg_load(reg_set, block, c, ir_reg);
                reg_set->locked |= (1 << c);
                reg_set->dirty[c] = 0;
        }

        reg_version[IREG_GET_REG(reg_set->regs[c].reg)][reg_set->regs[c].version].refcount--;
#ifndef RELEASE_BUILD
        if (reg_version[IREG_GET_REG(reg_set->regs[c].reg)][reg_set->regs[c].version].refcount == (uint8_t)-1)
                fatal("codegen_reg_alloc_read_reg - refcount < 0\n");
#endif

        if (host_reg_idx)
                *host_reg_idx = c;
        return reg_set->reg_list[c].reg | IREG_GET_SIZE(ir_reg.reg);
}

ir_host_reg_t codegen_reg_alloc_write_reg(codeblock_t *block, ir_reg_t ir_reg)
{
        host_reg_set_t *reg_set = get_reg_set(ir_reg);
        int c;

        if (!reg_is_native_size(ir_reg))
        {
                /*Read in parent register so we can do partial accesses to it*/
                ir_reg_t parent_reg;

                parent_reg.reg = IREG_GET_REG(ir_reg.reg) | IREG_SIZE_L;
                parent_reg.version = ir_reg.version - 1;
                reg_version[IREG_GET_REG(ir_reg.reg)][ir_reg.version - 1].refcount++;

                codegen_reg_alloc_read_reg(block, parent_reg, &c);

#ifndef RELEASE_BUILD
                if (IREG_GET_REG(reg_set->regs[c].reg) != IREG_GET_REG(ir_reg.reg) || reg_set->regs[c].version > ir_reg.version-1)
                        fatal("codegen_reg_alloc_write_reg sub_reg - doesn't match  %i %02x.%i %02x.%i\n", c,
                                        reg_set->regs[c].reg,reg_set->regs[c].version,
                                        ir_reg.reg,ir_reg.version);
#endif

                reg_set->regs[c].reg = ir_reg.reg;
                reg_set->regs[c].version = ir_reg.version;
                reg_set->dirty[c] = 1;
                return reg_set->reg_list[c].reg | IREG_GET_SIZE(ir_reg.reg);
        }

        /*Search for previous version in host register*/
        for (c = 0; c < reg_set->nr_regs; c++)
        {
                if (!ir_reg_is_invalid(reg_set->regs[c]) && IREG_GET_REG(reg_set->regs[c].reg) == IREG_GET_REG(ir_reg.reg))
                {
                        if (reg_set->regs[c].version <= ir_reg.version-1)
                        {
#ifndef RELEASE_BUILD
                                if (reg_version[IREG_GET_REG(reg_set->regs[c].reg)][reg_set->regs[c].version].refcount != 0)
                                        fatal("codegen_reg_alloc_write_reg - previous version refcount != 0\n");
#endif
                                break;
                        }
                }
        }

        if (c == reg_set->nr_regs)
        {
                /*Search for unused registers*/
                for (c = 0; c < reg_set->nr_regs; c++)
                {
                        if (ir_reg_is_invalid(reg_set->regs[c]))
                                break;
                }

                if (c == reg_set->nr_regs)
                {
                        /*No unused registers. Search for an unlocked register*/
                        for (c = 0; c < reg_set->nr_regs; c++)
                        {
                                if (!(reg_set->locked & (1 << c)))
                                        break;
                        }
#ifndef RELEASE_BUILD
                        if (c == reg_set->nr_regs)
                                fatal("codegen_reg_alloc_write_reg - out of registers\n");
#endif
                        if (reg_set->dirty[c])
                                codegen_reg_writeback(reg_set, block, c, 1);
                }
        }

        reg_set->regs[c].reg = ir_reg.reg;
        reg_set->regs[c].version = ir_reg.version;
        reg_set->dirty[c] = 1;
        return reg_set->reg_list[c].reg | IREG_GET_SIZE(ir_reg.reg);
}

#ifdef CODEGEN_BACKEND_HAS_MOV_IMM
int codegen_reg_is_loaded(ir_reg_t ir_reg)
{
        host_reg_set_t *reg_set = get_reg_set(ir_reg);
        int c;

        /*Search for previous version in host register*/
        for (c = 0; c < reg_set->nr_regs; c++)
        {
                if (!ir_reg_is_invalid(reg_set->regs[c]) && IREG_GET_REG(reg_set->regs[c].reg) == IREG_GET_REG(ir_reg.reg))
                {
                        if (reg_set->regs[c].version <= ir_reg.version-1)
                        {
#ifndef RELEASE_BUILD
                                if (reg_version[IREG_GET_REG(reg_set->regs[c].reg)][reg_set->regs[c].version].refcount != 0)
                                        fatal("codegen_reg_alloc_write_reg - previous version refcount != 0\n");
#endif
                                return 1;
                        }
                }
        }
        return 0;
}
#endif

void codegen_reg_rename(codeblock_t *block, ir_reg_t src, ir_reg_t dst)
{
        host_reg_set_t *reg_set = get_reg_set(src);
        int c;
        int target;

//        pclog("rename: %i.%i -> %i.%i\n", src.reg,src.version, dst.reg, dst.version);
        /*Search for required register*/
        for (c = 0; c < reg_set->nr_regs; c++)
        {
                if (!ir_reg_is_invalid(reg_set->regs[c]) && IREG_GET_REG(reg_set->regs[c].reg) == IREG_GET_REG(src.reg) && reg_set->regs[c].version == src.version)
                        break;
        }
#ifndef RELEASE_BUILD
        if (c == reg_set->nr_regs)
                fatal("codegen_reg_rename: Can't find register to rename\n");
#endif
        target = c;
        if (reg_set->dirty[target])
                codegen_reg_writeback(reg_set, block, target, 0);
        reg_set->regs[target] = dst;
        reg_set->dirty[target] = 1;
//        pclog("renamed reg %i dest=%i.%i\n", target, dst.reg, dst.version);

        /*Invalidate any stale copies of the dest register*/
        for (c = 0; c < reg_set->nr_regs; c++)
        {
                if (c == target)
                        continue;
                if (!ir_reg_is_invalid(reg_set->regs[c]) && IREG_GET_REG(reg_set->regs[c].reg) == IREG_GET_REG(dst.reg))
                {
                        reg_set->regs[c] = invalid_ir_reg;
                        reg_set->dirty[c] = 0;
                }
        }
}

void codegen_reg_flush(ir_data_t *ir, codeblock_t *block)
{
        host_reg_set_t *reg_set;
        int c;

        reg_set = &host_reg_set;
        for (c = 0; c < reg_set->nr_regs; c++)
        {
                if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c])
                {
                        codegen_reg_writeback(reg_set, block, c, 0);
                }
                if (reg_set->reg_list[c].flags & HOST_REG_FLAG_VOLATILE)
                {
                        reg_set->regs[c] = invalid_ir_reg;
                        reg_set->dirty[c] = 0;
                }
        }

        reg_set = &host_fp_reg_set;
        for (c = 0; c < reg_set->nr_regs; c++)
        {
                if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c])
                {
                        codegen_reg_writeback(reg_set, block, c, 0);
                }
                if (reg_set->reg_list[c].flags & HOST_REG_FLAG_VOLATILE)
                {
                        reg_set->regs[c] = invalid_ir_reg;
                        reg_set->dirty[c] = 0;
                }
        }
}

void codegen_reg_flush_invalidate(ir_data_t *ir, codeblock_t *block)
{
        host_reg_set_t *reg_set;
        int c;

        reg_set = &host_reg_set;
        for (c = 0; c < reg_set->nr_regs; c++)
        {
                if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c])
                {
                        codegen_reg_writeback(reg_set, block, c, 1);
                }
                reg_set->regs[c] = invalid_ir_reg;
                reg_set->dirty[c] = 0;
        }

        reg_set = &host_fp_reg_set;
        for (c = 0; c < reg_set->nr_regs; c++)
        {
                if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c])
                {
                        codegen_reg_writeback(reg_set, block, c, 1);
                }
                reg_set->regs[c] = invalid_ir_reg;
                reg_set->dirty[c] = 0;
        }
}

/*Process dead register list, and optimise out register versions and uOPs where
  possible*/
void codegen_reg_process_dead_list(ir_data_t *ir)
{
        while (reg_dead_list)
        {
                int version = reg_dead_list & 0xff;
                int reg = reg_dead_list >> 8;
                reg_version_t *regv = &reg_version[reg][version];
                uop_t *uop = &ir->uops[regv->parent_uop];

                /*Barrier uOPs should be preserved*/
                if (!(uop->type & (UOP_TYPE_BARRIER | UOP_TYPE_ORDER_BARRIER)))
                {
                        uop->type = UOP_INVALID;
                        /*Adjust refcounts on source registers. If these drop to
                          zero then those registers can be considered for removal*/
                        if (uop->src_reg_a.reg != IREG_INVALID)
                        {
                                reg_version_t *src_regv = &reg_version[IREG_GET_REG(uop->src_reg_a.reg)][uop->src_reg_a.version];
                                src_regv->refcount--;
                                if (!src_regv->refcount)
                                        add_to_dead_list(src_regv, IREG_GET_REG(uop->src_reg_a.reg), uop->src_reg_a.version);
                        }
                        if (uop->src_reg_b.reg != IREG_INVALID)
                        {
                                reg_version_t *src_regv = &reg_version[IREG_GET_REG(uop->src_reg_b.reg)][uop->src_reg_b.version];
                                src_regv->refcount--;
                                if (!src_regv->refcount)
                                        add_to_dead_list(src_regv, IREG_GET_REG(uop->src_reg_b.reg), uop->src_reg_b.version);
                        }
                        if (uop->src_reg_c.reg != IREG_INVALID)
                        {
                                reg_version_t *src_regv = &reg_version[IREG_GET_REG(uop->src_reg_c.reg)][uop->src_reg_c.version];
                                src_regv->refcount--;
                                if (!src_regv->refcount)
                                        add_to_dead_list(src_regv, IREG_GET_REG(uop->src_reg_c.reg), uop->src_reg_c.version);
                        }
                        regv->flags |= REG_FLAGS_DEAD;
                }

                reg_dead_list = regv->next;
        }
}
