#ifndef _CODEGEN_REG_H_
#define _CODEGEN_REG_H_

#define IREG_REG_MASK 0xff
#define IREG_SIZE_SHIFT 8
#define IREG_SIZE_MASK (7 << IREG_SIZE_SHIFT)

#define IREG_GET_REG(reg)  ((reg) & IREG_REG_MASK)
#define IREG_GET_SIZE(reg) ((reg) & IREG_SIZE_MASK)

#define IREG_SIZE_L  (0 << IREG_SIZE_SHIFT)
#define IREG_SIZE_W  (1 << IREG_SIZE_SHIFT)
#define IREG_SIZE_B  (2 << IREG_SIZE_SHIFT)
#define IREG_SIZE_BH (3 << IREG_SIZE_SHIFT)
#define IREG_SIZE_D  (4 << IREG_SIZE_SHIFT)
#define IREG_SIZE_Q  (5 << IREG_SIZE_SHIFT)

enum
{
	IREG_EAX = 0,
	IREG_ECX = 1,
	IREG_EDX = 2,
	IREG_EBX = 3,
	IREG_ESP = 4,
	IREG_EBP = 5,
	IREG_ESI = 6,
	IREG_EDI = 7,

	IREG_flags_op = 8,
	IREG_flags_res = 9,
	IREG_flags_op1 = 10,
	IREG_flags_op2 = 11,

	IREG_pc = 12,
	IREG_oldpc = 13,

	IREG_eaaddr = 14,
	IREG_ea_seg = 15,
	IREG_op32   = 16,
	IREG_ssegsx = 17,

	IREG_rm_mod_reg = 18,

	IREG_acycs = 19,
	IREG_cycles = 20,

        IREG_CS_base = 21,
        IREG_DS_base = 22,
        IREG_ES_base = 23,
        IREG_FS_base = 24,
        IREG_GS_base = 25,
        IREG_SS_base = 26,

        IREG_CS_seg = 27,
        IREG_DS_seg = 28,
        IREG_ES_seg = 29,
        IREG_FS_seg = 30,
        IREG_GS_seg = 31,
        IREG_SS_seg = 32,

	/*Temporary registers are stored on the stack, and are not guaranteed to
          be preserved across uOPs. They will not be written back if they will
          not be read again.*/
	IREG_temp0 = 33,
	IREG_temp1 = 34,
	IREG_temp2 = 35,
	IREG_temp3 = 36,

        IREG_FPU_TOP = 37,

	IREG_temp0d = 38,
	IREG_temp1d = 39,

        /*FPU stack registers are physical registers. Use IREG_ST() / IREG_tag()
          to access.
          When CODEBLOCK_STATIC_TOP is set, the physical register number will be
          used directly to index the stack. When it is clear, the difference
          between the current value of TOP and the value when the block was
          first compiled will be added to adjust for any changes in TOP.*/
        IREG_ST0 = 40,
        IREG_ST1 = 41,
        IREG_ST2 = 42,
        IREG_ST3 = 43,
        IREG_ST4 = 44,
        IREG_ST5 = 45,
        IREG_ST6 = 46,
        IREG_ST7 = 47,

        IREG_tag0 = 48,
        IREG_tag1 = 49,
        IREG_tag2 = 50,
        IREG_tag3 = 51,
        IREG_tag4 = 52,
        IREG_tag5 = 53,
        IREG_tag6 = 54,
        IREG_tag7 = 55,

        IREG_ST0_i64 = 56,
        IREG_ST1_i64 = 57,
        IREG_ST2_i64 = 58,
        IREG_ST3_i64 = 59,
        IREG_ST4_i64 = 60,
        IREG_ST5_i64 = 61,
        IREG_ST6_i64 = 62,
        IREG_ST7_i64 = 63,

        IREG_MM0x = 64,
        IREG_MM1x = 65,
        IREG_MM2x = 66,
        IREG_MM3x = 67,
        IREG_MM4x = 68,
        IREG_MM5x = 69,
        IREG_MM6x = 70,
        IREG_MM7x = 71,

        IREG_NPXCx = 72,
        IREG_NPXSx = 73,

        IREG_flagsx = 74,
        IREG_eflagsx = 75,

        IREG_CS_limit_low = 76,
        IREG_DS_limit_low = 77,
        IREG_ES_limit_low = 78,
        IREG_FS_limit_low = 79,
        IREG_GS_limit_low = 80,
        IREG_SS_limit_low = 81,

        IREG_CS_limit_high = 82,
        IREG_DS_limit_high = 83,
        IREG_ES_limit_high = 84,
        IREG_FS_limit_high = 85,
        IREG_GS_limit_high = 86,
        IREG_SS_limit_high = 87,

	IREG_COUNT = 88,

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

        IREG_flags = IREG_flagsx + IREG_SIZE_W,
        IREG_eflags = IREG_eflagsx + IREG_SIZE_W
};

#define IREG_8(reg)  (((reg) & 4) ? (((reg) & 3) + IREG_AH) : ((reg) + IREG_AL))
#define IREG_16(reg) ((reg) + IREG_AX)
#define IREG_32(reg) ((reg) + IREG_EAX)

#define IREG_ST(r)      (IREG_ST0     + ((cpu_state.TOP + (r)) & 7) + IREG_SIZE_D)
#define IREG_ST_i64(r)  (IREG_ST0_i64 + ((cpu_state.TOP + (r)) & 7) + IREG_SIZE_Q)
#define IREG_tag(r) (IREG_tag0 + ((cpu_state.TOP + (r)) & 7))
#define IREG_tag_B(r) (IREG_tag0 + ((cpu_state.TOP + (r)) & 7) + IREG_SIZE_B)

#define IREG_MM(reg) ((reg) + IREG_MM0)

#define IREG_TOP_diff_stack_offset 32

static inline int ireg_seg_base(x86seg *seg)
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

static inline int ireg_seg_limit_low(x86seg *seg)
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
static inline int ireg_seg_limit_high(x86seg *seg)
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

/*This version of the register must be calculated, regardless of whether it is
  apparently required or not. Do not optimise out.*/
#define REG_FLAGS_REQUIRED (1 << 0)
/*This register and the parent uOP have been optimised out.*/
#define REG_FLAGS_DEAD     (1 << 1)

typedef struct
{
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

static inline void add_to_dead_list(reg_version_t *regv, int reg, int version)
{
        regv->next = reg_dead_list;
        reg_dead_list = version | (reg << 8);
}

typedef struct
{
        uint16_t reg;
        uint16_t version;
} ir_reg_t;

extern ir_reg_t invalid_ir_reg;

typedef uint16_t ir_host_reg_t;

extern int max_version_refcount;

#define REG_VERSION_MAX 250
#define REG_REFCOUNT_MAX 250

static inline ir_reg_t codegen_reg_read(int reg)
{
        ir_reg_t ireg;
        reg_version_t *version;

#ifndef RELEASE_BUILD
        if (IREG_GET_REG(reg) == IREG_INVALID)
                fatal("codegen_reg_read - IREG_INVALID\n");
#endif
        ireg.reg = reg;
        ireg.version = reg_last_version[IREG_GET_REG(reg)];
        version = &reg_version[IREG_GET_REG(ireg.reg)][ireg.version];
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
//        pclog("codegen_reg_read: %i %i %i\n", reg & IREG_REG_MASK, ireg.version, reg_version_refcount[IREG_GET_REG(ireg.reg)][ireg.version]);
        return ireg;
}

int reg_is_native_size(ir_reg_t ir_reg);

static inline ir_reg_t codegen_reg_write(int reg, int uop_nr)
{
        ir_reg_t ireg;
        int last_version = reg_last_version[IREG_GET_REG(reg)];
        reg_version_t *version;

#ifndef RELEASE_BUILD
        if (IREG_GET_REG(reg) == IREG_INVALID)
                fatal("codegen_reg_write - IREG_INVALID\n");
#endif
        ireg.reg = reg;
        ireg.version = last_version + 1;

        if (IREG_GET_REG(reg) > IREG_EBX && last_version && !reg_version[IREG_GET_REG(reg)][last_version].refcount &&
                        !(reg_version[IREG_GET_REG(reg)][last_version].flags & REG_FLAGS_REQUIRED))
        {
                if (reg_is_native_size(ireg)) /*Non-native size registers have an implicit dependency on the previous version, so don't add to dead list*/
                        add_to_dead_list(&reg_version[IREG_GET_REG(reg)][last_version], IREG_GET_REG(reg), last_version);
        }

        reg_last_version[IREG_GET_REG(reg)]++;
#ifndef RELEASE_BUILD
        if (!reg_last_version[IREG_GET_REG(reg)])
                fatal("codegen_reg_write - version overflow\n");
        else
#endif
        if (reg_last_version[IREG_GET_REG(reg)] > REG_VERSION_MAX)
                CPU_BLOCK_END();
        if (reg_last_version[IREG_GET_REG(reg)] > max_version_refcount)
                max_version_refcount = reg_last_version[IREG_GET_REG(reg)];

        version = &reg_version[IREG_GET_REG(reg)][ireg.version];
        version->refcount = 0;
        version->flags = 0;
        version->parent_uop = uop_nr;
//        pclog("codegen_reg_write: %i\n", reg & IREG_REG_MASK);
        return ireg;
}

static inline int ir_reg_is_invalid(ir_reg_t ir_reg)
{
        return (IREG_GET_REG(ir_reg.reg) == IREG_INVALID);
}

struct ir_data_t;

void codegen_reg_reset();
/*Write back all dirty registers*/
void codegen_reg_flush(struct ir_data_t *ir, codeblock_t *block);
/*Write back and evict all registers*/
void codegen_reg_flush_invalidate(struct ir_data_t *ir, codeblock_t *block);

/*Register ir_reg usage for this uOP. This ensures that required registers aren't evicted*/
void codegen_reg_alloc_register(ir_reg_t dest_reg_a, ir_reg_t src_reg_a, ir_reg_t src_reg_b, ir_reg_t src_reg_c);

#ifdef CODEGEN_BACKEND_HAS_MOV_IMM
int codegen_reg_is_loaded(ir_reg_t ir_reg);
void codegen_reg_write_imm(codeblock_t *block, ir_reg_t ir_reg, uint32_t imm_data);
#endif

ir_host_reg_t codegen_reg_alloc_read_reg(codeblock_t *block, ir_reg_t ir_reg, int *host_reg_idx);
ir_host_reg_t codegen_reg_alloc_write_reg(codeblock_t *block, ir_reg_t ir_reg);

void codegen_reg_rename(codeblock_t *block, ir_reg_t src, ir_reg_t dst);

void codegen_reg_mark_as_required();
void codegen_reg_process_dead_list(struct ir_data_t *ir);
#endif
