#ifndef _CODEGEN_REG_H_
#define _CODEGEN_REG_H_

#define IREG_REG_MASK      0xff
#define IREG_SIZE_SHIFT    8
#define IREG_SIZE_MASK     (7 << IREG_SIZE_SHIFT)

#define IREG_GET_REG(reg)  ((reg) &IREG_REG_MASK)
#define IREG_GET_SIZE(reg) ((reg) &IREG_SIZE_MASK)

#define IREG_SIZE_L        (0 << IREG_SIZE_SHIFT)
#define IREG_SIZE_W        (1 << IREG_SIZE_SHIFT)
#define IREG_SIZE_B        (2 << IREG_SIZE_SHIFT)
#define IREG_SIZE_BH       (3 << IREG_SIZE_SHIFT)
#define IREG_SIZE_D        (4 << IREG_SIZE_SHIFT)
#define IREG_SIZE_Q        (5 << IREG_SIZE_SHIFT)

enum {
    IREG_EAX,
    IREG_ECX,
    IREG_EDX,
    IREG_EBX,
    IREG_ESP,
    IREG_EBP,
    IREG_ESI,
    IREG_EDI,

    IREG_flags_op,
    IREG_flags_res,
    IREG_flags_op1,
    IREG_flags_op2,

    IREG_pc,
    IREG_oldpc,

    IREG_eaaddr,
    IREG_ea_seg,
    IREG_op32,
    IREG_ssegsx,

    IREG_rm_mod_reg,

    IREG_cycles,

    IREG_CS_base,
    IREG_DS_base,
    IREG_ES_base,
    IREG_FS_base,
    IREG_GS_base,
    IREG_SS_base,

    IREG_CS_seg,
    IREG_DS_seg,
    IREG_ES_seg,
    IREG_FS_seg,
    IREG_GS_seg,
    IREG_SS_seg,

    /*FPU stack registers are physical registers. Use IREG_ST() / IREG_tag()
      to access.
      When CODEBLOCK_STATIC_TOP is set, the physical register number will be
      used directly to index the stack. When it is clear, the difference
      between the current value of TOP and the value when the block was
      first compiled will be added to adjust for any changes in TOP.*/
    IREG_ST0,
    IREG_ST1,
    IREG_ST2,
    IREG_ST3,
    IREG_ST4,
    IREG_ST5,
    IREG_ST6,
    IREG_ST7,

    IREG_tag0,
    IREG_tag1,
    IREG_tag2,
    IREG_tag3,
    IREG_tag4,
    IREG_tag5,
    IREG_tag6,
    IREG_tag7,

    IREG_ST0_i64,
    IREG_ST1_i64,
    IREG_ST2_i64,
    IREG_ST3_i64,
    IREG_ST4_i64,
    IREG_ST5_i64,
    IREG_ST6_i64,
    IREG_ST7_i64,

    IREG_MM0x,
    IREG_MM1x,
    IREG_MM2x,
    IREG_MM3x,
    IREG_MM4x,
    IREG_MM5x,
    IREG_MM6x,
    IREG_MM7x,

    IREG_NPXCx,
    IREG_NPXSx,

    IREG_flagsx,
    IREG_eflagsx,

    IREG_CS_limit_low,
    IREG_DS_limit_low,
    IREG_ES_limit_low,
    IREG_FS_limit_low,
    IREG_GS_limit_low,
    IREG_SS_limit_low,

    IREG_CS_limit_high,
    IREG_DS_limit_high,
    IREG_ES_limit_high,
    IREG_FS_limit_high,
    IREG_GS_limit_high,
    IREG_SS_limit_high,

    IREG_eaa16,
    IREG_x87_op,

    IREG_FPU_TOP,

    /*Temporary registers are stored on the stack, and are not guaranteed to
      be preserved across uOPs. They will not be written back if they will
      not be read again.*/
    IREG_temp0,
    IREG_temp1,
    IREG_temp2,
    IREG_temp3,

    IREG_temp0d,
    IREG_temp1d,

    IREG_COUNT,

    IREG_INVALID = 255,

    IREG_AX = IREG_EAX + IREG_SIZE_W,
    IREG_CX = IREG_ECX + IREG_SIZE_W,
    IREG_DX = IREG_EDX + IREG_SIZE_W,
    IREG_BX = IREG_EBX + IREG_SIZE_W,
    IREG_SP = IREG_ESP + IREG_SIZE_W,
    IREG_BP = IREG_EBP + IREG_SIZE_W,
    IREG_SI = IREG_ESI + IREG_SIZE_W,
    IREG_DI = IREG_EDI + IREG_SIZE_W,

    IREG_AL = IREG_EAX + IREG_SIZE_B,
    IREG_CL = IREG_ECX + IREG_SIZE_B,
    IREG_DL = IREG_EDX + IREG_SIZE_B,
    IREG_BL = IREG_EBX + IREG_SIZE_B,

    IREG_AH = IREG_EAX + IREG_SIZE_BH,
    IREG_CH = IREG_ECX + IREG_SIZE_BH,
    IREG_DH = IREG_EDX + IREG_SIZE_BH,
    IREG_BH = IREG_EBX + IREG_SIZE_BH,

    IREG_flags_res_W = IREG_flags_res + IREG_SIZE_W,
    IREG_flags_op1_W = IREG_flags_op1 + IREG_SIZE_W,
    IREG_flags_op2_W = IREG_flags_op2 + IREG_SIZE_W,

    IREG_flags_res_B = IREG_flags_res + IREG_SIZE_B,
    IREG_flags_op1_B = IREG_flags_op1 + IREG_SIZE_B,
    IREG_flags_op2_B = IREG_flags_op2 + IREG_SIZE_B,

    IREG_temp0_W = IREG_temp0 + IREG_SIZE_W,
    IREG_temp1_W = IREG_temp1 + IREG_SIZE_W,
    IREG_temp2_W = IREG_temp2 + IREG_SIZE_W,
    IREG_temp3_W = IREG_temp3 + IREG_SIZE_W,

    IREG_temp0_B = IREG_temp0 + IREG_SIZE_B,
    IREG_temp1_B = IREG_temp1 + IREG_SIZE_B,
    IREG_temp2_B = IREG_temp2 + IREG_SIZE_B,
    IREG_temp3_B = IREG_temp3 + IREG_SIZE_B,

    IREG_temp0_D = IREG_temp0d + IREG_SIZE_D,
    IREG_temp1_D = IREG_temp1d + IREG_SIZE_D,

    IREG_temp0_Q = IREG_temp0d + IREG_SIZE_Q,
    IREG_temp1_Q = IREG_temp1d + IREG_SIZE_Q,

    IREG_eaaddr_W = IREG_eaaddr + IREG_SIZE_W,

    IREG_CS_seg_W = IREG_CS_seg + IREG_SIZE_W,
    IREG_DS_seg_W = IREG_DS_seg + IREG_SIZE_W,
    IREG_ES_seg_W = IREG_ES_seg + IREG_SIZE_W,
    IREG_FS_seg_W = IREG_FS_seg + IREG_SIZE_W,
    IREG_GS_seg_W = IREG_GS_seg + IREG_SIZE_W,
    IREG_SS_seg_W = IREG_SS_seg + IREG_SIZE_W,

    IREG_MM0 = IREG_MM0x + IREG_SIZE_Q,
    IREG_MM1 = IREG_MM1x + IREG_SIZE_Q,
    IREG_MM2 = IREG_MM2x + IREG_SIZE_Q,
    IREG_MM3 = IREG_MM3x + IREG_SIZE_Q,
    IREG_MM4 = IREG_MM4x + IREG_SIZE_Q,
    IREG_MM5 = IREG_MM5x + IREG_SIZE_Q,
    IREG_MM6 = IREG_MM6x + IREG_SIZE_Q,
    IREG_MM7 = IREG_MM7x + IREG_SIZE_Q,

    IREG_NPXC = IREG_NPXCx + IREG_SIZE_W,
    IREG_NPXS = IREG_NPXSx + IREG_SIZE_W,

    IREG_ssegs = IREG_ssegsx + IREG_SIZE_B,

    IREG_flags  = IREG_flagsx + IREG_SIZE_W,
    IREG_eflags = IREG_eflagsx + IREG_SIZE_W
};

#define IREG_8(reg)                (((reg) &4) ? (((reg) &3) + IREG_AH) : ((reg) + IREG_AL))
#define IREG_16(reg)               ((reg) + IREG_AX)
#define IREG_32(reg)               ((reg) + IREG_EAX)

#define IREG_ST(r)                 (IREG_ST0 + ((cpu_state.TOP + (r)) & 7) + IREG_SIZE_D)
#define IREG_ST_i64(r)             (IREG_ST0_i64 + ((cpu_state.TOP + (r)) & 7) + IREG_SIZE_Q)
#define IREG_tag(r)                (IREG_tag0 + ((cpu_state.TOP + (r)) & 7))
#define IREG_tag_B(r)              (IREG_tag0 + ((cpu_state.TOP + (r)) & 7) + IREG_SIZE_B)

#define IREG_MM(reg)               ((reg) + IREG_MM0)

#define IREG_TOP_diff_stack_offset 32

static inline int
ireg_seg_base(x86seg *seg)
{
    if (seg == &cpu_state.seg_cs)
        return IREG_CS_base;
    if (seg == &cpu_state.seg_ds)
        return IREG_DS_base;
    if (seg == &cpu_state.seg_es)
        return IREG_ES_base;
    if (seg == &cpu_state.seg_fs)
        return IREG_FS_base;
    if (seg == &cpu_state.seg_gs)
        return IREG_GS_base;
    if (seg == &cpu_state.seg_ss)
        return IREG_SS_base;
    fatal("ireg_seg_base : unknown segment\n");
    return 0;
}

static inline int
ireg_seg_limit_low(x86seg *seg)
{
    if (seg == &cpu_state.seg_cs)
        return IREG_CS_limit_low;
    if (seg == &cpu_state.seg_ds)
        return IREG_DS_limit_low;
    if (seg == &cpu_state.seg_es)
        return IREG_ES_limit_low;
    if (seg == &cpu_state.seg_fs)
        return IREG_FS_limit_low;
    if (seg == &cpu_state.seg_gs)
        return IREG_GS_limit_low;
    if (seg == &cpu_state.seg_ss)
        return IREG_SS_limit_low;
    fatal("ireg_seg_limit_low : unknown segment\n");
    return 0;
}
static inline int
ireg_seg_limit_high(x86seg *seg)
{
    if (seg == &cpu_state.seg_cs)
        return IREG_CS_limit_high;
    if (seg == &cpu_state.seg_ds)
        return IREG_DS_limit_high;
    if (seg == &cpu_state.seg_es)
        return IREG_ES_limit_high;
    if (seg == &cpu_state.seg_fs)
        return IREG_FS_limit_high;
    if (seg == &cpu_state.seg_gs)
        return IREG_GS_limit_high;
    if (seg == &cpu_state.seg_ss)
        return IREG_SS_limit_high;
    fatal("ireg_seg_limit_high : unknown segment\n");
    return 0;
}

extern uint8_t reg_last_version[IREG_COUNT];
extern uint64_t dirty_ir_regs[2];

/*This version of the register must be calculated, regardless of whether it is
  apparently required or not. Do not optimise out.*/
#define REG_FLAGS_REQUIRED (1 << 0)
/*This register and the parent uOP have been optimised out.*/
#define REG_FLAGS_DEAD (1 << 1)

typedef struct {
    /*Refcount of pending reads on this register version*/
    uint8_t refcount;
    /*Flags*/
    uint8_t flags;
    /*uOP that generated this register version*/
    uint16_t parent_uop;
    /*Pointer to next register version in dead register list*/
    uint16_t next;
} reg_version_t;

extern reg_version_t reg_version[IREG_COUNT][256];

/*Head of dead register list; a list of register versions that are not used and
  can be optimised out*/
extern uint16_t reg_dead_list;

static inline void
add_to_dead_list(reg_version_t *regv, int reg, int version)
{
    regv->next    = reg_dead_list;
    reg_dead_list = version | (reg << 8);
}

typedef struct {
    uint16_t reg;
    uint16_t version;
} ir_reg_t;

extern ir_reg_t invalid_ir_reg;

typedef uint16_t ir_host_reg_t;

extern int max_version_refcount;

#define REG_VERSION_MAX  250
#define REG_REFCOUNT_MAX 250

static inline ir_reg_t
codegen_reg_read(int reg)
{
    ir_reg_t       ireg;
    reg_version_t *version;

#ifndef RELEASE_BUILD
    if (IREG_GET_REG(reg) == IREG_INVALID)
        fatal("codegen_reg_read - IREG_INVALID\n");
#endif
    ireg.reg       = reg;
    ireg.version   = reg_last_version[IREG_GET_REG(reg)];
    version        = &reg_version[IREG_GET_REG(ireg.reg)][ireg.version];
    version->flags = 0;
    version->refcount++;
#ifndef RELEASE_BUILD
    if (!version->refcount)
        fatal("codegen_reg_read - refcount overflow\n");
    else
#endif
        if (version->refcount > REG_REFCOUNT_MAX)
        CPU_BLOCK_END();
    if (version->refcount > max_version_refcount)
        max_version_refcount = version->refcount;
#if 0
    pclog("codegen_reg_read: %i %i %i\n", reg & IREG_REG_MASK, ireg.version, reg_version_refcount[IREG_GET_REG(ireg.reg)][ireg.version]);
#endif
    return ireg;
}

int reg_is_native_size(ir_reg_t ir_reg);

static inline ir_reg_t
codegen_reg_write(int reg, int uop_nr)
{
    ir_reg_t       ireg;
    int            last_version = reg_last_version[IREG_GET_REG(reg)];
    reg_version_t *version;

    if (dirty_ir_regs[(IREG_GET_REG(reg) >> 6) & 3] & (1ull << ((uint64_t)IREG_GET_REG(reg) & 0x3full))) {
        dirty_ir_regs[(IREG_GET_REG(reg) >> 6) & 3] &= ~(1ull << ((uint64_t)IREG_GET_REG(reg) & 0x3full));
        if ((IREG_GET_REG(reg) > IREG_EBX && IREG_GET_REG(reg) < IREG_temp0) && last_version > 0) {
            reg_version[IREG_GET_REG(reg)][last_version].flags |= REG_FLAGS_REQUIRED;
        }
    }
    ireg.reg     = reg;
    ireg.version = last_version + 1;

    if (IREG_GET_REG(reg) > IREG_EBX && last_version && !reg_version[IREG_GET_REG(reg)][last_version].refcount && !(reg_version[IREG_GET_REG(reg)][last_version].flags & REG_FLAGS_REQUIRED)) {
        if (reg_is_native_size(ireg)) /*Non-native size registers have an implicit dependency on the previous version, so don't add to dead list*/
            add_to_dead_list(&reg_version[IREG_GET_REG(reg)][last_version], IREG_GET_REG(reg), last_version);
    }

    reg_last_version[IREG_GET_REG(reg)]++;

    if (reg_last_version[IREG_GET_REG(reg)] > REG_VERSION_MAX)
        CPU_BLOCK_END();
    if (reg_last_version[IREG_GET_REG(reg)] > max_version_refcount)
        max_version_refcount = reg_last_version[IREG_GET_REG(reg)];

    version             = &reg_version[IREG_GET_REG(reg)][ireg.version];
    version->refcount   = 0;
    version->flags      = 0;
    version->parent_uop = uop_nr;
#if 0
    pclog("codegen_reg_write: %i\n", reg & IREG_REG_MASK);
#endif
    return ireg;
}

static inline int
ir_reg_is_invalid(ir_reg_t ir_reg)
{
    return (IREG_GET_REG(ir_reg.reg) == IREG_INVALID);
}

struct ir_data_t;

void codegen_reg_reset(void);
/*Write back all dirty registers*/
void codegen_reg_flush(struct ir_data_t *ir, codeblock_t *block);
/*Write back and evict all registers*/
void codegen_reg_flush_invalidate(struct ir_data_t *ir, codeblock_t *block);

/*Register ir_reg usage for this uOP. This ensures that required registers aren't evicted*/
void codegen_reg_alloc_register(ir_reg_t dest_reg_a, ir_reg_t src_reg_a, ir_reg_t src_reg_b, ir_reg_t src_reg_c);

#ifdef CODEGEN_BACKEND_HAS_MOV_IMM
int  codegen_reg_is_loaded(ir_reg_t ir_reg);
void codegen_reg_write_imm(codeblock_t *block, ir_reg_t ir_reg, uint32_t imm_data);
#endif

ir_host_reg_t codegen_reg_alloc_read_reg(codeblock_t *block, ir_reg_t ir_reg, int *host_reg_idx);
ir_host_reg_t codegen_reg_alloc_write_reg(codeblock_t *block, ir_reg_t ir_reg);

void codegen_reg_rename(codeblock_t *block, ir_reg_t src, ir_reg_t dst);

void codegen_reg_mark_as_required(void);
void codegen_reg_process_dead_list(struct ir_data_t *ir);
#endif
