#include <stdint.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "codegen.h"
#include "codegen_backend.h"
#include "codegen_ir_defs.h"
#include "codegen_reg.h"

int      max_version_refcount;
uint16_t reg_dead_list = 0;

uint8_t       reg_last_version[IREG_COUNT];
reg_version_t reg_version[IREG_COUNT][256];

ir_reg_t invalid_ir_reg = { IREG_INVALID };

ir_reg_t       _host_regs[CODEGEN_HOST_REGS];
static uint8_t _host_reg_dirty[CODEGEN_HOST_REGS];

ir_reg_t       host_fp_regs[CODEGEN_HOST_FP_REGS];
static uint8_t host_fp_reg_dirty[CODEGEN_HOST_FP_REGS];

typedef struct host_reg_set_t {
    ir_reg_t       *regs;
    uint8_t        *dirty;
    host_reg_def_t *reg_list;
    uint16_t        locked;
    int             nr_regs;
    /*Bit c set while regs[c] holds a version of that IR register, so a
      search for one visits only the host registers that hold it, in the
      order a walk over all of them would*/
    uint16_t        slots[IREG_COUNT];
} host_reg_set_t;

static host_reg_set_t host_reg_set;
static host_reg_set_t host_fp_reg_set;

/*Every store to regs[] goes through here to keep slots[] in step*/
static inline void
host_reg_assign(host_reg_set_t *reg_set, int c, ir_reg_t ir_reg)
{
    int old_reg = IREG_GET_REG(reg_set->regs[c].reg);
    int new_reg = IREG_GET_REG(ir_reg.reg);

    if (old_reg != IREG_INVALID)
        reg_set->slots[old_reg] &= ~(1 << c);
    if (new_reg != IREG_INVALID)
        reg_set->slots[new_reg] |= (1 << c);
    reg_set->regs[c] = ir_reg;
}

uint64_t dirty_ir_regs[2] = { 0, 0 };

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
/*Immediates not stored yet, oldest first. Each is stored before anything
  loads or stores memory it overlaps, and at every full flush, so cpu_state
  takes the stores in the order it does without them*/
static struct {
    uint32_t imm;
    uint8_t  ireg;
} lazy_pending[CODEGEN_LAZY_PENDING_MAX];
static int nr_lazy_pending;

static codegen_lazy_list_t lazy_lists[CODEGEN_LAZY_SITES_MAX];
static int                 nr_lazy_lists;
codegen_lazy_list_t       *codegen_lazy_site;

/*Bit r set while IR register r has an entry in lazy_pending[] (one at most)*/
static uint64_t lazy_pending_mask[2];

/*The tables below never change: codegen_reg_lazy_init() builds them once*/
/*Registers of the guest's state that a stub can store from the host
  register they are in: no C reached from a lazy uop on its way on reads
  one, and the uop leaves its host register alone until its exit branch.
  Never IREG_cycles, and never in a host register a C call does not
  preserve (lazy_volatile_slots)*/
static uint8_t ireg_lazy_deferrable[IREG_COUNT];
/*IR registers an immediate is stored to at once rather than held*/
static uint8_t ireg_imm_direct[IREG_COUNT];
/*Bit j of ireg_mem_overlap[i] set when j's memory overlaps i's*/
static uint64_t ireg_mem_overlap[IREG_COUNT][2];
/*Integer host registers a C call does not preserve*/
static uint16_t lazy_volatile_slots;

static void codegen_reg_lazy_init(void);
#endif

enum {
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

enum {
    REG_INTEGER,
    REG_FP
};

enum {
    /*Register may be accessed outside of code block, and must be written
      back before any control transfers*/
    REG_PERMANENT = 0,
    /*Register will not be accessed outside of code block, and does not need
      to be written back if there are no readers remaining*/
    REG_VOLATILE = 1
};

struct
{
    int   native_size;
    void *p;
    int   type;
    int   is_volatile;
} ireg_data[IREG_COUNT] = {
    [IREG_EAX] = {REG_DWORD,          &EAX,                               REG_INTEGER, REG_PERMANENT},
    [IREG_ECX] = { REG_DWORD,         &ECX,                               REG_INTEGER, REG_PERMANENT},
    [IREG_EDX] = { REG_DWORD,         &EDX,                               REG_INTEGER, REG_PERMANENT},
    [IREG_EBX] = { REG_DWORD,         &EBX,                               REG_INTEGER, REG_PERMANENT},
    [IREG_ESP] = { REG_DWORD,         &ESP,                               REG_INTEGER, REG_PERMANENT},
    [IREG_EBP] = { REG_DWORD,         &EBP,                               REG_INTEGER, REG_PERMANENT},
    [IREG_ESI] = { REG_DWORD,         &ESI,                               REG_INTEGER, REG_PERMANENT},
    [IREG_EDI] = { REG_DWORD,         &EDI,                               REG_INTEGER, REG_PERMANENT},

    [IREG_flags_op]  = { REG_DWORD,         &cpu_state.flags_op,                REG_INTEGER, REG_PERMANENT},
    [IREG_flags_res] = { REG_DWORD,         &cpu_state.flags_res,               REG_INTEGER, REG_PERMANENT},
    [IREG_flags_op1] = { REG_DWORD,         &cpu_state.flags_op1,               REG_INTEGER, REG_PERMANENT},
    [IREG_flags_op2] = { REG_DWORD,         &cpu_state.flags_op2,               REG_INTEGER, REG_PERMANENT},

    [IREG_pc]    = { REG_DWORD,         &cpu_state.pc,                      REG_INTEGER, REG_PERMANENT},
    [IREG_oldpc] = { REG_DWORD,         &cpu_state.oldpc,                   REG_INTEGER, REG_PERMANENT},

    [IREG_eaaddr] = { REG_DWORD,         &cpu_state.eaaddr,                  REG_INTEGER, REG_PERMANENT},
    [IREG_ea_seg] = { REG_POINTER,       &cpu_state.ea_seg,                  REG_INTEGER, REG_PERMANENT},

    [IREG_op32]   = { REG_DWORD,         &cpu_state.op32,                    REG_INTEGER, REG_PERMANENT},
    [IREG_ssegsx] = { REG_BYTE,          &cpu_state.ssegs,                   REG_INTEGER, REG_PERMANENT},

    [IREG_rm_mod_reg] = { REG_DWORD,         &cpu_state.rm_data.rm_mod_reg_data, REG_INTEGER, REG_PERMANENT},

    [IREG_cycles] = { REG_DWORD,         &cpu_state._cycles,                 REG_INTEGER, REG_PERMANENT},

    [IREG_CS_base] = { REG_DWORD,         &cpu_state.seg_cs.base,             REG_INTEGER, REG_PERMANENT},
    [IREG_DS_base] = { REG_DWORD,         &cpu_state.seg_ds.base,             REG_INTEGER, REG_PERMANENT},
    [IREG_ES_base] = { REG_DWORD,         &cpu_state.seg_es.base,             REG_INTEGER, REG_PERMANENT},
    [IREG_FS_base] = { REG_DWORD,         &cpu_state.seg_fs.base,             REG_INTEGER, REG_PERMANENT},
    [IREG_GS_base] = { REG_DWORD,         &cpu_state.seg_gs.base,             REG_INTEGER, REG_PERMANENT},
    [IREG_SS_base] = { REG_DWORD,         &cpu_state.seg_ss.base,             REG_INTEGER, REG_PERMANENT},

    [IREG_CS_seg] = { REG_WORD,          &cpu_state.seg_cs.seg,              REG_INTEGER, REG_PERMANENT},
    [IREG_DS_seg] = { REG_WORD,          &cpu_state.seg_ds.seg,              REG_INTEGER, REG_PERMANENT},
    [IREG_ES_seg] = { REG_WORD,          &cpu_state.seg_es.seg,              REG_INTEGER, REG_PERMANENT},
    [IREG_FS_seg] = { REG_WORD,          &cpu_state.seg_fs.seg,              REG_INTEGER, REG_PERMANENT},
    [IREG_GS_seg] = { REG_WORD,          &cpu_state.seg_gs.seg,              REG_INTEGER, REG_PERMANENT},
    [IREG_SS_seg] = { REG_WORD,          &cpu_state.seg_ss.seg,              REG_INTEGER, REG_PERMANENT},

    [IREG_FPU_TOP] = { REG_DWORD,         &cpu_state.TOP,                     REG_INTEGER, REG_PERMANENT},

    [IREG_ST0] = { REG_FPU_ST_DOUBLE, &cpu_state.ST[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST1] = { REG_FPU_ST_DOUBLE, &cpu_state.ST[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST2] = { REG_FPU_ST_DOUBLE, &cpu_state.ST[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST3] = { REG_FPU_ST_DOUBLE, &cpu_state.ST[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST4] = { REG_FPU_ST_DOUBLE, &cpu_state.ST[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST5] = { REG_FPU_ST_DOUBLE, &cpu_state.ST[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST6] = { REG_FPU_ST_DOUBLE, &cpu_state.ST[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST7] = { REG_FPU_ST_DOUBLE, &cpu_state.ST[0],                   REG_FP,      REG_PERMANENT},

    [IREG_tag0] = { REG_FPU_ST_BYTE,   &cpu_state.tag[0],                  REG_INTEGER, REG_PERMANENT},
    [IREG_tag1] = { REG_FPU_ST_BYTE,   &cpu_state.tag[0],                  REG_INTEGER, REG_PERMANENT},
    [IREG_tag2] = { REG_FPU_ST_BYTE,   &cpu_state.tag[0],                  REG_INTEGER, REG_PERMANENT},
    [IREG_tag3] = { REG_FPU_ST_BYTE,   &cpu_state.tag[0],                  REG_INTEGER, REG_PERMANENT},
    [IREG_tag4] = { REG_FPU_ST_BYTE,   &cpu_state.tag[0],                  REG_INTEGER, REG_PERMANENT},
    [IREG_tag5] = { REG_FPU_ST_BYTE,   &cpu_state.tag[0],                  REG_INTEGER, REG_PERMANENT},
    [IREG_tag6] = { REG_FPU_ST_BYTE,   &cpu_state.tag[0],                  REG_INTEGER, REG_PERMANENT},
    [IREG_tag7] = { REG_FPU_ST_BYTE,   &cpu_state.tag[0],                  REG_INTEGER, REG_PERMANENT},

    [IREG_ST0_i64] = { REG_FPU_ST_QWORD,  &cpu_state.MM[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST1_i64] = { REG_FPU_ST_QWORD,  &cpu_state.MM[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST2_i64] = { REG_FPU_ST_QWORD,  &cpu_state.MM[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST3_i64] = { REG_FPU_ST_QWORD,  &cpu_state.MM[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST4_i64] = { REG_FPU_ST_QWORD,  &cpu_state.MM[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST5_i64] = { REG_FPU_ST_QWORD,  &cpu_state.MM[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST6_i64] = { REG_FPU_ST_QWORD,  &cpu_state.MM[0],                   REG_FP,      REG_PERMANENT},
    [IREG_ST7_i64] = { REG_FPU_ST_QWORD,  &cpu_state.MM[0],                   REG_FP,      REG_PERMANENT},

    [IREG_MM0x] = { REG_QWORD,         &cpu_state.MM[0],                   REG_FP,      REG_PERMANENT},
    [IREG_MM1x] = { REG_QWORD,         &cpu_state.MM[1],                   REG_FP,      REG_PERMANENT},
    [IREG_MM2x] = { REG_QWORD,         &cpu_state.MM[2],                   REG_FP,      REG_PERMANENT},
    [IREG_MM3x] = { REG_QWORD,         &cpu_state.MM[3],                   REG_FP,      REG_PERMANENT},
    [IREG_MM4x] = { REG_QWORD,         &cpu_state.MM[4],                   REG_FP,      REG_PERMANENT},
    [IREG_MM5x] = { REG_QWORD,         &cpu_state.MM[5],                   REG_FP,      REG_PERMANENT},
    [IREG_MM6x] = { REG_QWORD,         &cpu_state.MM[6],                   REG_FP,      REG_PERMANENT},
    [IREG_MM7x] = { REG_QWORD,         &cpu_state.MM[7],                   REG_FP,      REG_PERMANENT},

    [IREG_NPXCx] = { REG_WORD,          &cpu_state.npxc,                    REG_INTEGER, REG_PERMANENT},
    [IREG_NPXSx] = { REG_WORD,          &cpu_state.npxs,                    REG_INTEGER, REG_PERMANENT},

    [IREG_flagsx]  = { REG_WORD,          &cpu_state.flags,                   REG_INTEGER, REG_PERMANENT},
    [IREG_eflagsx] = { REG_WORD,          &cpu_state.eflags,                  REG_INTEGER, REG_PERMANENT},

    [IREG_CS_limit_low] = { REG_DWORD,         &cpu_state.seg_cs.limit_low,        REG_INTEGER, REG_PERMANENT},
    [IREG_DS_limit_low] = { REG_DWORD,         &cpu_state.seg_ds.limit_low,        REG_INTEGER, REG_PERMANENT},
    [IREG_ES_limit_low] = { REG_DWORD,         &cpu_state.seg_es.limit_low,        REG_INTEGER, REG_PERMANENT},
    [IREG_FS_limit_low] = { REG_DWORD,         &cpu_state.seg_fs.limit_low,        REG_INTEGER, REG_PERMANENT},
    [IREG_GS_limit_low] = { REG_DWORD,         &cpu_state.seg_gs.limit_low,        REG_INTEGER, REG_PERMANENT},
    [IREG_SS_limit_low] = { REG_DWORD,         &cpu_state.seg_ss.limit_low,        REG_INTEGER, REG_PERMANENT},

    [IREG_CS_limit_high] = { REG_DWORD,         &cpu_state.seg_cs.limit_high,       REG_INTEGER, REG_PERMANENT},
    [IREG_DS_limit_high] = { REG_DWORD,         &cpu_state.seg_ds.limit_high,       REG_INTEGER, REG_PERMANENT},
    [IREG_ES_limit_high] = { REG_DWORD,         &cpu_state.seg_es.limit_high,       REG_INTEGER, REG_PERMANENT},
    [IREG_FS_limit_high] = { REG_DWORD,         &cpu_state.seg_fs.limit_high,       REG_INTEGER, REG_PERMANENT},
    [IREG_GS_limit_high] = { REG_DWORD,         &cpu_state.seg_gs.limit_high,       REG_INTEGER, REG_PERMANENT},
    [IREG_SS_limit_high] = { REG_DWORD,         &cpu_state.seg_ss.limit_high,       REG_INTEGER, REG_PERMANENT},

    [IREG_eaa16] = { REG_WORD,         &cpu_state.eaaddr,                  REG_INTEGER, REG_PERMANENT},
    [IREG_x87_op] = { REG_DWORD,        &x87_op,                            REG_INTEGER, REG_PERMANENT},

    [IREG_fpu_op] = { REG_WORD,         &cpu_state.fpu_op,                            REG_INTEGER, REG_PERMANENT},
    [IREG_fpu_CS] = { REG_WORD,         &cpu_state.fpu_CS,                            REG_INTEGER, REG_PERMANENT},
    [IREG_fpu_cs] = { REG_DWORD,        &cpu_state.fpu_cs,                            REG_INTEGER, REG_PERMANENT},
    [IREG_fpu_pc] = { REG_DWORD,        &cpu_state.fpu_pc,                            REG_INTEGER, REG_PERMANENT},
    [IREG_sf_exc] = { REG_DWORD,        &cpu_state.sf_exc,                            REG_INTEGER, REG_PERMANENT},

    [IREG_x87_operandx] = { REG_DOUBLE, &cpu_state.x87_operand,                       REG_FP,      REG_PERMANENT},

 /*Temporary registers are stored on the stack, and are not guaranteed to
  be preserved across uOPs. They will not be written back if they will
  not be read again. On Windows x64 they sit above the 32 bytes of home
  space a called function may store its arguments in.*/
    [IREG_temp0] = { REG_DWORD,         (void *) (16 + CODEGEN_TEMP_BASE),  REG_INTEGER, REG_VOLATILE },
    [IREG_temp1] = { REG_DWORD,         (void *) (20 + CODEGEN_TEMP_BASE),  REG_INTEGER, REG_VOLATILE },
    [IREG_temp2] = { REG_DWORD,         (void *) (24 + CODEGEN_TEMP_BASE),  REG_INTEGER, REG_VOLATILE },
    [IREG_temp3] = { REG_DWORD,         (void *) (28 + CODEGEN_TEMP_BASE),  REG_INTEGER, REG_VOLATILE },

    [IREG_temp0d] = { REG_DOUBLE,        (void *) (40 + CODEGEN_TEMP_BASE),  REG_FP,      REG_VOLATILE },
    [IREG_temp1d] = { REG_DOUBLE,        (void *) (48 + CODEGEN_TEMP_BASE),  REG_FP,      REG_VOLATILE },
    [IREG_temp2d] = { REG_DOUBLE,        (void *) (56 + CODEGEN_TEMP_BASE),  REG_FP,      REG_VOLATILE },
};

static const uint8_t native_requested_sizes[9][8] = 
{
    [REG_BYTE][IREG_SIZE_B >> IREG_SIZE_SHIFT]          = 1,
    [REG_FPU_ST_BYTE][IREG_SIZE_B >> IREG_SIZE_SHIFT]   = 1,
    [REG_WORD][IREG_SIZE_W >> IREG_SIZE_SHIFT]          = 1,
    [REG_DWORD][IREG_SIZE_L >> IREG_SIZE_SHIFT]         = 1,
    [REG_QWORD][IREG_SIZE_D >> IREG_SIZE_SHIFT]         = 1,
    [REG_FPU_ST_QWORD][IREG_SIZE_D >> IREG_SIZE_SHIFT]  = 1,
    [REG_DOUBLE][IREG_SIZE_D >> IREG_SIZE_SHIFT]        = 1,
    [REG_FPU_ST_DOUBLE][IREG_SIZE_D >> IREG_SIZE_SHIFT] = 1,
    [REG_QWORD][IREG_SIZE_Q >> IREG_SIZE_SHIFT]         = 1,
    [REG_FPU_ST_QWORD][IREG_SIZE_Q >> IREG_SIZE_SHIFT]  = 1,
    [REG_DOUBLE][IREG_SIZE_Q >> IREG_SIZE_SHIFT]        = 1,
    [REG_FPU_ST_DOUBLE][IREG_SIZE_Q >> IREG_SIZE_SHIFT] = 1,

#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64 || defined __loongarch_lp64
    [REG_POINTER][IREG_SIZE_Q >> IREG_SIZE_SHIFT] = 1
#else
    [REG_POINTER][(sizeof(void *) == 4) ? (IREG_SIZE_L >> IREG_SIZE_SHIFT) : (IREG_SIZE_Q >> IREG_SIZE_SHIFT)] = 1
#endif
};

/*The IR registers that hold state outside the block (REG_PERMANENT): a
  version of one may be read by C code the IR does not see -- a helper, the
  interpreter after the block -- so it is kept once such code may have run,
  wherever it sits in the enumeration (IREG_x87_operandx and IREG_fpu_op..
  IREG_sf_exc come after the temporaries)*/
uint64_t ireg_permanent_mask[2];

/*0xff for each permanent register from IREG_ESP up, 0 for the others. Built
  once with ireg_permanent_mask by codegen_check_regs(): ireg_data never
  changes*/
static uint8_t ireg_mark_bytes[IREG_COUNT];

_Static_assert(sizeof(reg_last_version[0]) == 1, "codegen_reg_mark_as_required() reads reg_last_version 8 bytes at a time");
_Static_assert((IREG_COUNT % 8) == 0, "codegen_reg_mark_as_required() reads reg_last_version 8 bytes at a time");

void
codegen_reg_mark_as_required(void)
{
    /* This used to start from IREG_EAX, now only starts from IREG_ESP since the first 4 registers are never optimized out. */
    /* It also no longer iterates through volatile registers unnecessarily. */
    /*Most registers are not written in a block: a group of 8 with no
      written one to mark is passed over whole. Only whether the 64-bit word
      is zero is used, so the host's byte order does not matter, and the
      registers are still visited in ascending order*/
    for (int group = 0; group < IREG_COUNT; group += 8) {
        uint64_t written;
        uint64_t marked;

        memcpy(&written, &reg_last_version[group], 8);
        memcpy(&marked, &ireg_mark_bytes[group], 8);
        if (!(written & marked))
            continue;

        for (int reg = group; reg < (group + 8); reg++) {
            int last_version = reg_last_version[reg];

            if (ireg_mark_bytes[reg] && (last_version > 0))
                reg_version[reg][last_version].flags |= REG_FLAGS_REQUIRED;
        }
    }
    dirty_ir_regs[0] = dirty_ir_regs[1] = 0;
}

/*IR registers that some uOP reads or writes by byte: the guest's AL-BH,
  the flags operands, the temporaries, the byte-sized state, and the
  registers with a byte view defined (IREG_fpu_op_b, IREG_sf_exc_b).*/
int
ireg_is_byte_accessed(int ireg)
{
    switch (ireg) {
        case IREG_EAX:
        case IREG_ECX:
        case IREG_EDX:
        case IREG_EBX:
        case IREG_flags_res:
        case IREG_flags_op1:
        case IREG_flags_op2:
        case IREG_temp0:
        case IREG_temp1:
        case IREG_temp2:
        case IREG_temp3:
        case IREG_fpu_op:
        case IREG_sf_exc:
            return 1;

        default:
            return (ireg_data[ireg].native_size == REG_BYTE) || (ireg_data[ireg].native_size == REG_FPU_ST_BYTE);
    }
}

static inline int
host_reg_can_hold(const host_reg_set_t *reg_set, int c, ir_reg_t ir_reg)
{
    return !(reg_set->reg_list[c].flags & HOST_REG_FLAG_NO_BYTE) || !ireg_is_byte_accessed(IREG_GET_REG(ir_reg.reg));
}

int
reg_is_native_size(ir_reg_t ir_reg)
{
    int native_size    = ireg_data[IREG_GET_REG(ir_reg.reg)].native_size;
    int requested_size = IREG_GET_SIZE(ir_reg.reg);

    return native_requested_sizes[native_size][requested_size >> IREG_SIZE_SHIFT];
}

void
codegen_check_regs(void)
{
    int i = 0;

    ireg_permanent_mask[0] = ireg_permanent_mask[1] = 0;
    for (i = 0; i < IREG_COUNT; i++) {
        if (ireg_data[i].is_volatile == REG_PERMANENT)
            ireg_permanent_mask[i >> 6] |= 1ull << (i & 0x3f);
        ireg_mark_bytes[i] = ((i >= IREG_ESP) && (ireg_data[i].is_volatile == REG_PERMANENT)) ? 0xff : 0;
    }
#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    codegen_reg_lazy_init();
#endif

    for (i = 0; i < IREG_COUNT; i++) {
        if (ireg_data[i].is_volatile == REG_VOLATILE)
            continue;

        if (ireg_data[i].p && ((uintptr_t)ireg_data[i].p - (uintptr_t)&cpu_state) >= sizeof(cpu_state)) {
            fatal("Register number %d outside cpu_state!\n", i);
        }
    }
}

void
codegen_reg_reset(void)
{
    int c;

    host_reg_set.regs        = _host_regs;
    host_reg_set.dirty       = _host_reg_dirty;
    host_reg_set.reg_list    = codegen_host_reg_list;
    host_reg_set.locked      = 0;
    host_reg_set.nr_regs     = CODEGEN_HOST_REGS;
    host_fp_reg_set.regs     = host_fp_regs;
    host_fp_reg_set.dirty    = host_fp_reg_dirty;
    host_fp_reg_set.reg_list = codegen_host_fp_reg_list;
    host_fp_reg_set.locked   = 0;
    host_fp_reg_set.nr_regs  = CODEGEN_HOST_FP_REGS;

    dirty_ir_regs[0] = dirty_ir_regs[1] = 0;

    for (c = 0; c < IREG_COUNT; c++) {
        reg_last_version[c]        = 0;
        reg_version[c][0].refcount = 0;
        reg_version[c][0].uncounted = 0;
    }
    for (c = 0; c < CODEGEN_HOST_REGS; c++) {
        host_reg_assign(&host_reg_set, c, invalid_ir_reg);
        host_reg_set.dirty[c] = 0;
    }
    for (c = 0; c < CODEGEN_HOST_FP_REGS; c++) {
        host_reg_assign(&host_fp_reg_set, c, invalid_ir_reg);
        host_fp_reg_set.dirty[c] = 0;
    }

    reg_dead_list        = 0;
    max_version_refcount = 0;

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    nr_lazy_pending   = 0;
    nr_lazy_lists     = 0;
    lazy_pending_mask[0] = lazy_pending_mask[1] = 0;
    codegen_lazy_site = NULL;
#endif
}

static inline int
ir_get_refcount(ir_reg_t ir_reg)
{
    return reg_version[IREG_GET_REG(ir_reg.reg)][ir_reg.version].refcount;
}

static inline host_reg_set_t *
get_reg_set(ir_reg_t ir_reg)
{
    if (ireg_data[IREG_GET_REG(ir_reg.reg)].type == REG_INTEGER)
        return &host_reg_set;
    else
        return &host_fp_reg_set;
}

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
static void lazy_pending_store_overlapping(codeblock_t *block, int ireg);
static void lazy_pending_store_all(codeblock_t *block);
static void lazy_pending_drop(int ireg);
#endif

static void
codegen_reg_load(host_reg_set_t *reg_set, codeblock_t *block, int c, ir_reg_t ir_reg)
{
#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    lazy_pending_store_overlapping(block, IREG_GET_REG(ir_reg.reg));
#endif
    switch (ireg_data[IREG_GET_REG(ir_reg.reg)].native_size) {
        case REG_WORD:
#ifndef RELEASE_BUILD
            if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_INTEGER)
                fatal("codegen_reg_load - REG_WORD !REG_INTEGER\n");
#endif
            if ((uintptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                codegen_direct_read_16_stack(block, reg_set->reg_list[c].reg, (intptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p);
            else
                codegen_direct_read_16(block, reg_set->reg_list[c].reg, ireg_data[IREG_GET_REG(ir_reg.reg)].p);
            break;

        case REG_DWORD:
#ifndef RELEASE_BUILD
            if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_INTEGER)
                fatal("codegen_reg_load - REG_DWORD !REG_INTEGER\n");
#endif
            if ((uintptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                codegen_direct_read_32_stack(block, reg_set->reg_list[c].reg, (intptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p);
            else
                codegen_direct_read_32(block, reg_set->reg_list[c].reg, ireg_data[IREG_GET_REG(ir_reg.reg)].p);
            break;

        case REG_QWORD:
#ifndef RELEASE_BUILD
            if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_FP)
                fatal("codegen_reg_load - REG_QWORD !REG_FP\n");
#endif
            if ((uintptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                codegen_direct_read_64_stack(block, reg_set->reg_list[c].reg, (intptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p);
            else
                codegen_direct_read_64(block, reg_set->reg_list[c].reg, ireg_data[IREG_GET_REG(ir_reg.reg)].p);
            break;

        case REG_POINTER:
#ifndef RELEASE_BUILD
            if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_INTEGER)
                fatal("codegen_reg_load - REG_POINTER !REG_INTEGER\n");
#endif
            if ((uintptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                codegen_direct_read_pointer_stack(block, reg_set->reg_list[c].reg, (intptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p);
            else
                codegen_direct_read_pointer(block, reg_set->reg_list[c].reg, ireg_data[IREG_GET_REG(ir_reg.reg)].p);
            break;

        case REG_DOUBLE:
#ifndef RELEASE_BUILD
            if (ireg_data[IREG_GET_REG(ir_reg.reg)].type != REG_FP)
                fatal("codegen_reg_load - REG_DOUBLE !REG_FP\n");
#endif
            if ((uintptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p < 256)
                codegen_direct_read_double_stack(block, reg_set->reg_list[c].reg, (intptr_t) ireg_data[IREG_GET_REG(ir_reg.reg)].p);
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

    host_reg_assign(reg_set, c, ir_reg);
}

static void
codegen_reg_writeback(host_reg_set_t *reg_set, codeblock_t *block, int c, int invalidate)
{
    int   ir_reg = IREG_GET_REG(reg_set->regs[c].reg);
    void *p      = ireg_data[ir_reg].p;

    if (!reg_version[ir_reg][reg_set->regs[c].version].refcount && ireg_data[ir_reg].is_volatile)
        return;

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    lazy_pending_store_overlapping(block, ir_reg);
    /*The lazy uop's dest allocation evicting a register on its list: it is
      in memory now, and its host register may be loaded with another before
      the uop's exit branch*/
    if (codegen_lazy_site && (reg_set == &host_reg_set)) {
        codegen_lazy_list_t *list = codegen_lazy_site;
        int                  i    = 0;

        while (i < list->nr) {
            if (list->stores[i].host_reg == reg_set->reg_list[c].reg) {
                list->nr--;
                for (int j = i; j < list->nr; j++)
                    list->stores[j] = list->stores[j + 1];
            } else
                i++;
        }
    }
#endif
    switch (ireg_data[ir_reg].native_size) {
        case REG_BYTE:
#ifndef RELEASE_BUILD
            if (ireg_data[ir_reg].type != REG_INTEGER)
                fatal("codegen_reg_writeback - REG_BYTE !REG_INTEGER\n");
            if ((uintptr_t) p < 256)
                fatal("codegen_reg_writeback - REG_BYTE %p\n", p);
#endif
            codegen_direct_write_8(block, p, reg_set->reg_list[c].reg);
            break;

        case REG_WORD:
#ifndef RELEASE_BUILD
            if (ireg_data[ir_reg].type != REG_INTEGER)
                fatal("codegen_reg_writeback - REG_WORD !REG_INTEGER\n");
            if ((uintptr_t) p < 256)
                fatal("codegen_reg_writeback - REG_WORD %p\n", p);
#endif
            codegen_direct_write_16(block, p, reg_set->reg_list[c].reg);
            break;

        case REG_DWORD:
#ifndef RELEASE_BUILD
            if (ireg_data[ir_reg].type != REG_INTEGER)
                fatal("codegen_reg_writeback - REG_DWORD !REG_INTEGER\n");
#endif
            if ((uintptr_t) p < 256)
                codegen_direct_write_32_stack(block, (intptr_t) p, reg_set->reg_list[c].reg);
            else
                codegen_direct_write_32(block, p, reg_set->reg_list[c].reg);
            break;

        case REG_QWORD:
#ifndef RELEASE_BUILD
            if (ireg_data[ir_reg].type != REG_FP)
                fatal("codegen_reg_writeback - REG_QWORD !REG_FP\n");
#endif
            if ((uintptr_t) p < 256)
                codegen_direct_write_64_stack(block, (intptr_t) p, reg_set->reg_list[c].reg);
            else
                codegen_direct_write_64(block, p, reg_set->reg_list[c].reg);
            break;

        case REG_POINTER:
#ifndef RELEASE_BUILD
            if (ireg_data[ir_reg].type != REG_INTEGER)
                fatal("codegen_reg_writeback - REG_POINTER !REG_INTEGER\n");
            if ((uintptr_t) p < 256)
                fatal("codegen_reg_writeback - REG_POINTER %p\n", p);
#endif
            codegen_direct_write_ptr(block, p, reg_set->reg_list[c].reg);
            break;

        case REG_DOUBLE:
#ifndef RELEASE_BUILD
            if (ireg_data[ir_reg].type != REG_FP)
                fatal("codegen_reg_writeback - REG_DOUBLE !REG_FP\n");
#endif
            if ((uintptr_t) p < 256)
                codegen_direct_write_double_stack(block, (intptr_t) p, reg_set->reg_list[c].reg);
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
        host_reg_assign(reg_set, c, invalid_ir_reg);
    reg_set->dirty[c] = 0;
}

#ifdef CODEGEN_BACKEND_HAS_MOV_IMM
void
codegen_reg_write_imm(codeblock_t *block, ir_reg_t ir_reg, uint32_t imm_data)
{
    int   reg_idx = IREG_GET_REG(ir_reg.reg);
    void *p       = ireg_data[reg_idx].p;

    switch (ireg_data[reg_idx].native_size) {
        case REG_BYTE:
#    ifndef RELEASE_BUILD
            if ((uintptr_t) p < 256)
                fatal("codegen_reg_write_imm - REG_BYTE %p\n", p);
#    endif
            codegen_direct_write_8_imm(block, p, imm_data);
            break;

        case REG_WORD:
#    ifndef RELEASE_BUILD
            if ((uintptr_t) p < 256)
                fatal("codegen_reg_write_imm - REG_WORD %p\n", p);
#    endif
            codegen_direct_write_16_imm(block, p, imm_data);
            break;

        case REG_DWORD:
            if ((uintptr_t) p < 256)
                codegen_direct_write_32_imm_stack(block, (int) ((uintptr_t) p), imm_data);
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

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
static int
ireg_mem_size(int ireg)
{
    switch (ireg_data[ireg].native_size) {
        case REG_BYTE:
            return 1;
        case REG_WORD:
            return 2;
        case REG_DWORD:
            return 4;
        case REG_POINTER:
            return sizeof(void *);
        case REG_FPU_ST_BYTE:
            return 8;
        case REG_FPU_ST_QWORD:
        case REG_FPU_ST_DOUBLE:
            return 64;
        default:
            return 8;
    }
}

static int
ireg_mem_overlaps(int ireg_a, int ireg_b)
{
    uintptr_t a = (uintptr_t) ireg_data[ireg_a].p;
    uintptr_t b = (uintptr_t) ireg_data[ireg_b].p;

    if (a < 256 || b < 256)
        return 0;
    return (a < (b + ireg_mem_size(ireg_b))) && (b < (a + ireg_mem_size(ireg_a)));
}

static void
codegen_reg_lazy_init(void)
{
    lazy_volatile_slots = 0;
    for (int c = 0; c < CODEGEN_HOST_REGS; c++) {
        if (codegen_host_reg_list[c].flags & HOST_REG_FLAG_VOLATILE)
            lazy_volatile_slots |= (1 << c);
    }

    for (int i = 0; i < IREG_COUNT; i++) {
        ireg_lazy_deferrable[i] = 0;
        if ((i != IREG_cycles) && !ireg_data[i].is_volatile) {
            switch (ireg_data[i].native_size) {
                case REG_BYTE:
                case REG_WORD:
                case REG_DWORD:
                case REG_POINTER:
                    ireg_lazy_deferrable[i] = (uintptr_t) ireg_data[i].p >= 256;
                    break;
                default:
                    break;
            }
        }

        ireg_imm_direct[i] = ireg_data[i].is_volatile || ((uintptr_t) ireg_data[i].p < 256);

        ireg_mem_overlap[i][0] = ireg_mem_overlap[i][1] = 0;
        for (int j = 0; j < IREG_COUNT; j++) {
            if (ireg_mem_overlaps(j, i))
                ireg_mem_overlap[i][j >> 6] |= 1ull << (j & 0x3f);
        }
    }
}

static void
lazy_pending_remove(int i)
{
    lazy_pending_mask[lazy_pending[i].ireg >> 6] &= ~(1ull << (lazy_pending[i].ireg & 0x3f));
    nr_lazy_pending--;
    for (; i < nr_lazy_pending; i++)
        lazy_pending[i] = lazy_pending[i + 1];
}

static void
lazy_pending_store(codeblock_t *block, int i)
{
    ir_reg_t ir_reg;

    ir_reg.reg     = lazy_pending[i].ireg;
    ir_reg.version = 0;
    codegen_reg_write_imm(block, ir_reg, lazy_pending[i].imm);
    lazy_pending_remove(i);
}

static void
lazy_pending_store_all(codeblock_t *block)
{
    while (nr_lazy_pending)
        lazy_pending_store(block, 0);
}

/*Store the pending immediates whose memory overlaps ireg's, before ireg is
  loaded from or stored to it*/
static void
lazy_pending_store_overlapping(codeblock_t *block, int ireg)
{
    int i = 0;

    if (!((lazy_pending_mask[0] & ireg_mem_overlap[ireg][0]) | (lazy_pending_mask[1] & ireg_mem_overlap[ireg][1])))
        return;
    while (i < nr_lazy_pending) {
        if (ireg_mem_overlaps(lazy_pending[i].ireg, ireg))
            lazy_pending_store(block, i);
        else
            i++;
    }
}

/*ireg gets a whole new value in a host register, which supersedes its
  pending immediate*/
static void
lazy_pending_drop(int ireg)
{
    if (!(lazy_pending_mask[ireg >> 6] & (1ull << (ireg & 0x3f))))
        return;
    for (int i = 0; i < nr_lazy_pending; i++) {
        if (lazy_pending[i].ireg == ireg) {
            lazy_pending_remove(i);
            return;
        }
    }
}

void
codegen_reg_defer_imm(codeblock_t *block, ir_reg_t ir_reg, uint32_t imm_data)
{
    int ireg = IREG_GET_REG(ir_reg.reg);

    if (ireg_imm_direct[ireg]) {
        codegen_reg_write_imm(block, ir_reg, imm_data);
        return;
    }
    lazy_pending_drop(ireg);
    if (nr_lazy_pending == CODEGEN_LAZY_PENDING_MAX)
        lazy_pending_store(block, 0);
    lazy_pending[nr_lazy_pending].ireg = ireg;
    lazy_pending[nr_lazy_pending].imm  = imm_data;
    nr_lazy_pending++;
    lazy_pending_mask[ireg >> 6] |= 1ull << (ireg & 0x3f);
}

static void
lazy_store_host(codeblock_t *block, int ireg, int host_reg)
{
    void *p = ireg_data[ireg].p;

    switch (ireg_data[ireg].native_size) {
        case REG_BYTE:
            codegen_direct_write_8(block, p, host_reg);
            break;
        case REG_WORD:
            codegen_direct_write_16(block, p, host_reg);
            break;
        case REG_DWORD:
            codegen_direct_write_32(block, p, host_reg);
            break;
        case REG_POINTER:
            codegen_direct_write_ptr(block, p, host_reg);
            break;
        default:
            fatal("lazy_store_host - native_size=%i\n", ireg_data[ireg].native_size);
    }
}

void
codegen_reg_lazy_stores(codeblock_t *block, const codegen_lazy_list_t *list)
{
    for (int i = 0; i < list->nr; i++) {
        const codegen_lazy_store_t *store = &list->stores[i];

        if (store->host_reg == CODEGEN_LAZY_IMM) {
            ir_reg_t ir_reg;

            ir_reg.reg     = store->ireg;
            ir_reg.version = 0;
            codegen_reg_write_imm(block, ir_reg, store->imm);
        } else
            lazy_store_host(block, store->ireg, store->host_reg);
    }
}
#endif

static void
alloc_reg(ir_reg_t ir_reg)
{
    host_reg_set_t *reg_set = get_reg_set(ir_reg);
    uint16_t        slots   = reg_set->slots[IREG_GET_REG(ir_reg.reg)];

    if (slots) {
        int c = __builtin_ctz(slots);

#ifndef RELEASE_BUILD
        if (reg_set->regs[c].version != ir_reg.version)
            fatal("alloc_reg - host_regs[c].version != ir_reg.version  %i %p %p  %i %i\n", c, reg_set, &host_reg_set, reg_set->regs[c].reg, ir_reg.reg);
#endif
        reg_set->locked |= (1 << c);
    }
}

static void
alloc_dest_reg(ir_reg_t ir_reg, int dest_reference)
{
    host_reg_set_t *reg_set = get_reg_set(ir_reg);
    uint16_t        slots   = reg_set->slots[IREG_GET_REG(ir_reg.reg)];

    if (slots) {
        int c = __builtin_ctz(slots);

        if (reg_set->regs[c].version == ir_reg.version) {
            reg_set->locked |= (1 << c);
        } else {
            /*The immediate prior version may have been
              optimised out, so search backwards to find the
              last valid version*/
            int prev_version = ir_reg.version - 1;
            while (prev_version >= 0) {
                const reg_version_t *regv = &reg_version[IREG_GET_REG(reg_set->regs[c].reg)][prev_version];

                if (!(regv->flags & REG_FLAGS_DEAD) && regv->refcount == dest_reference) {
                    reg_set->locked |= (1 << c);
                    return;
                }
                prev_version--;
            }
            fatal("codegen_reg_alloc_register - host_regs[c].version != dest_reg_a.version  %i,%i %i\n", reg_set->regs[c].version, ir_reg.version, dest_reference);
        }
    }
}

void
codegen_reg_alloc_register(ir_reg_t dest_reg_a, ir_reg_t src_reg_a, ir_reg_t src_reg_b, ir_reg_t src_reg_c)
{
    int dest_reference = 0;

    host_reg_set.locked    = 0;
    host_fp_reg_set.locked = 0;

    if (!ir_reg_is_invalid(dest_reg_a)) {
        if (!ir_reg_is_invalid(src_reg_a) && IREG_GET_REG(src_reg_a.reg) == IREG_GET_REG(dest_reg_a.reg) && src_reg_a.version == dest_reg_a.version - 1)
            dest_reference++;
        if (!ir_reg_is_invalid(src_reg_b) && IREG_GET_REG(src_reg_b.reg) == IREG_GET_REG(dest_reg_a.reg) && src_reg_b.version == dest_reg_a.version - 1)
            dest_reference++;
        if (!ir_reg_is_invalid(src_reg_c) && IREG_GET_REG(src_reg_c.reg) == IREG_GET_REG(dest_reg_a.reg) && src_reg_c.version == dest_reg_a.version - 1)
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

ir_host_reg_t
codegen_reg_alloc_read_reg(codeblock_t *block, ir_reg_t ir_reg, int *host_reg_idx)
{
    host_reg_set_t *reg_set = get_reg_set(ir_reg);
    int             c;

    /*Search for required register*/
    c = reg_set->nr_regs;
    for (uint16_t slots = reg_set->slots[IREG_GET_REG(ir_reg.reg)]; slots; slots &= slots - 1) {
        int s = __builtin_ctz(slots);

        if (reg_set->regs[s].version == ir_reg.version) {
            c = s;
            break;
        }

        if (reg_set->regs[s].version <= ir_reg.version) {
            reg_version[IREG_GET_REG(reg_set->regs[s].reg)][reg_set->regs[s].version].refcount++;
            c = s;
            break;
        }

#ifndef RELEASE_BUILD
        if (reg_version[IREG_GET_REG(reg_set->regs[s].reg)][reg_set->regs[s].version].refcount)
            fatal("codegen_reg_alloc_read_reg - version mismatch!\n");
#endif
    }

    if (c == reg_set->nr_regs) {
        /*Search for unused registers*/
        for (c = 0; c < reg_set->nr_regs; c++) {
            if (ir_reg_is_invalid(reg_set->regs[c]) && host_reg_can_hold(reg_set, c, ir_reg))
                break;
        }
        if (c == reg_set->nr_regs) {
            /*No unused registers. Search for an unlocked register with no pending reads*/
            for (c = 0; c < reg_set->nr_regs; c++) {
                if (!(reg_set->locked & (1 << c)) && IREG_GET_REG(reg_set->regs[c].reg) != IREG_INVALID && !ir_get_refcount(reg_set->regs[c]) && host_reg_can_hold(reg_set, c, ir_reg))
                    break;
            }
        }
        if (c == reg_set->nr_regs) {
            /*Search for any unlocked register*/
            for (c = 0; c < reg_set->nr_regs; c++) {
                if (!(reg_set->locked & (1 << c)) && host_reg_can_hold(reg_set, c, ir_reg))
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
    if (reg_version[IREG_GET_REG(reg_set->regs[c].reg)][reg_set->regs[c].version].refcount == (uint8_t) -1)
        fatal("codegen_reg_alloc_read_reg - refcount < 0\n");
#endif

    if (host_reg_idx)
        *host_reg_idx = c;
    return reg_set->reg_list[c].reg | IREG_GET_SIZE(ir_reg.reg);
}

ir_host_reg_t
codegen_reg_alloc_write_reg(codeblock_t *block, ir_reg_t ir_reg)
{
    host_reg_set_t *reg_set = get_reg_set(ir_reg);
    int             c;

    if (!reg_is_native_size(ir_reg)) {
        /*Read in parent register so we can do partial accesses to it*/
        ir_reg_t parent_reg;

        parent_reg.reg     = IREG_GET_REG(ir_reg.reg) | IREG_SIZE_L;
        parent_reg.version = ir_reg.version - 1;
        reg_version[IREG_GET_REG(ir_reg.reg)][ir_reg.version - 1].refcount++;

        codegen_reg_alloc_read_reg(block, parent_reg, &c);

#ifndef RELEASE_BUILD
        if (IREG_GET_REG(reg_set->regs[c].reg) != IREG_GET_REG(ir_reg.reg) || reg_set->regs[c].version > ir_reg.version - 1)
            fatal("codegen_reg_alloc_write_reg sub_reg - doesn't match  %i %02x.%i %02x.%i\n", c,
                  reg_set->regs[c].reg, reg_set->regs[c].version,
                  ir_reg.reg, ir_reg.version);
#endif

        host_reg_assign(reg_set, c, ir_reg);
        reg_set->dirty[c] = 1;
        return reg_set->reg_list[c].reg | IREG_GET_SIZE(ir_reg.reg);
    }

    /*Search for previous version in host register*/
    c = reg_set->nr_regs;
    for (uint16_t slots = reg_set->slots[IREG_GET_REG(ir_reg.reg)]; slots; slots &= slots - 1) {
        int s = __builtin_ctz(slots);

        if (reg_set->regs[s].version <= ir_reg.version - 1) {
#ifndef RELEASE_BUILD
            if (reg_version[IREG_GET_REG(reg_set->regs[s].reg)][reg_set->regs[s].version].refcount != 0)
                fatal("codegen_reg_alloc_write_reg - previous version refcount != 0\n");
#endif
            c = s;
            break;
        }
    }

    if (c == reg_set->nr_regs) {
        /*Search for unused registers*/
        for (c = 0; c < reg_set->nr_regs; c++) {
            if (ir_reg_is_invalid(reg_set->regs[c]) && host_reg_can_hold(reg_set, c, ir_reg))
                break;
        }

        if (c == reg_set->nr_regs) {
            /*No unused registers. Search for an unlocked register*/
            for (c = 0; c < reg_set->nr_regs; c++) {
                if (!(reg_set->locked & (1 << c)) && host_reg_can_hold(reg_set, c, ir_reg))
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

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    lazy_pending_drop(IREG_GET_REG(ir_reg.reg));
#endif
    host_reg_assign(reg_set, c, ir_reg);
    reg_set->dirty[c] = 1;
    return reg_set->reg_list[c].reg | IREG_GET_SIZE(ir_reg.reg);
}

#ifdef CODEGEN_BACKEND_HAS_MOV_IMM
int
codegen_reg_is_loaded(ir_reg_t ir_reg)
{
    const host_reg_set_t *reg_set = get_reg_set(ir_reg);

    /*Search for previous version in host register*/
    for (uint16_t slots = reg_set->slots[IREG_GET_REG(ir_reg.reg)]; slots; slots &= slots - 1) {
        int c = __builtin_ctz(slots);

        if (reg_set->regs[c].version <= ir_reg.version - 1) {
#    ifndef RELEASE_BUILD
            if (reg_version[IREG_GET_REG(reg_set->regs[c].reg)][reg_set->regs[c].version].refcount != 0)
                fatal("codegen_reg_alloc_write_reg - previous version refcount != 0\n");
#    endif
            return 1;
        }
    }
    return 0;
}
#endif

void
codegen_reg_rename(codeblock_t *block, ir_reg_t src, ir_reg_t dst)
{
    host_reg_set_t *reg_set = get_reg_set(src);
    int             c;
    int             target;

#if 0
    pclog("rename: %i.%i -> %i.%i\n", src.reg,src.version, dst.reg, dst.version);
#endif

    /*Search for required register*/
    c = reg_set->nr_regs;
    for (uint16_t slots = reg_set->slots[IREG_GET_REG(src.reg)]; slots; slots &= slots - 1) {
        int s = __builtin_ctz(slots);

        if (reg_set->regs[s].version == src.version) {
            c = s;
            break;
        }
    }
#ifndef RELEASE_BUILD
    if (c == reg_set->nr_regs)
        fatal("codegen_reg_rename: Can't find register to rename\n");
#endif
    target = c;
    if (reg_set->dirty[target])
        codegen_reg_writeback(reg_set, block, target, 0);
#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    lazy_pending_drop(IREG_GET_REG(dst.reg));
#endif
    host_reg_assign(reg_set, target, dst);
    reg_set->dirty[target] = 1;
#if 0
    pclog("renamed reg %i dest=%i.%i\n", target, dst.reg, dst.version);
#endif

    /*Invalidate any stale copies of the dest register*/
    for (uint16_t slots = reg_set->slots[IREG_GET_REG(dst.reg)] & ~(1 << target); slots; slots &= slots - 1) {
        c = __builtin_ctz(slots);
        host_reg_assign(reg_set, c, invalid_ir_reg);
        reg_set->dirty[c] = 0;
    }
}

/*The C code behind an order-barrier uop (a memory slow path charging
  timing_misaligned or wait states, a helper) can change cycles in memory,
  which a copy kept in a host register would then overwrite at the next
  cycle flush.*/
void
codegen_reg_drop_cycles(void)
{
    host_reg_set_t *reg_set = &host_reg_set;

    for (int c = 0; c < reg_set->nr_regs; c++) {
        if (!ir_reg_is_invalid(reg_set->regs[c]) && IREG_GET_REG(reg_set->regs[c].reg) == IREG_cycles && !reg_set->dirty[c])
            host_reg_assign(reg_set, c, invalid_ir_reg);
    }
}

void
codegen_reg_flush(UNUSED(ir_data_t *ir), codeblock_t *block)
{
    host_reg_set_t *reg_set;
    int             c;

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    lazy_pending_store_all(block);
#endif
    reg_set = &host_reg_set;
    for (c = 0; c < reg_set->nr_regs; c++) {
        if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c]) {
            codegen_reg_writeback(reg_set, block, c, 0);
        }
        /*The C code behind an ordered access (a memory access's slow path,
          a device's handler) charges its own cycles to cpu_state._cycles: a
          copy kept past it would be written back over them*/
        if ((reg_set->reg_list[c].flags & HOST_REG_FLAG_VOLATILE) ||
            (!ir_reg_is_invalid(reg_set->regs[c]) && (IREG_GET_REG(reg_set->regs[c].reg) == IREG_cycles))) {
            host_reg_assign(reg_set, c, invalid_ir_reg);
            reg_set->dirty[c] = 0;
        }
    }

    reg_set = &host_fp_reg_set;
    for (c = 0; c < reg_set->nr_regs; c++) {
        if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c]) {
            codegen_reg_writeback(reg_set, block, c, 0);
        }
        if (reg_set->reg_list[c].flags & HOST_REG_FLAG_VOLATILE) {
            host_reg_assign(reg_set, c, invalid_ir_reg);
            reg_set->dirty[c] = 0;
        }
    }
}

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
void
codegen_reg_flush_lazy(ir_data_t *ir, codeblock_t *block)
{
    host_reg_set_t      *reg_set;
    codegen_lazy_list_t *list;
    int                  c;
    uint16_t             deferred = 0;
    uint16_t             written  = 0;

    codegen_lazy_site = NULL;
    if (nr_lazy_lists == CODEGEN_LAZY_SITES_MAX) {
        codegen_reg_flush(ir, block);
        return;
    }

    /*What is not deferred goes first, as codegen_reg_flush does it, so the
      pending immediates it stores are off the list. A write-back changes
      only its own host register's dirty flag, so the dirty registers can be
      sorted before any is written back*/
    reg_set = &host_reg_set;
    for (c = 0; c < reg_set->nr_regs; c++) {
        if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c]) {
            if (!(lazy_volatile_slots & (1 << c)) && ireg_lazy_deferrable[IREG_GET_REG(reg_set->regs[c].reg)])
                deferred |= (1 << c);
            else
                written |= (1 << c);
        }
    }
    for (; written; written &= written - 1)
        codegen_reg_writeback(reg_set, block, __builtin_ctz(written), 0);
    for (uint16_t dropped = lazy_volatile_slots | reg_set->slots[IREG_cycles]; dropped; dropped &= dropped - 1) {
        c = __builtin_ctz(dropped);
        host_reg_assign(reg_set, c, invalid_ir_reg);
        reg_set->dirty[c] = 0;
    }

    reg_set = &host_fp_reg_set;
    for (c = 0; c < reg_set->nr_regs; c++) {
        if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c]) {
            codegen_reg_writeback(reg_set, block, c, 0);
        }
        if (reg_set->reg_list[c].flags & HOST_REG_FLAG_VOLATILE) {
            host_reg_assign(reg_set, c, invalid_ir_reg);
            reg_set->dirty[c] = 0;
        }
    }

    list     = &lazy_lists[nr_lazy_lists];
    list->nr = 0;
    for (c = 0; c < nr_lazy_pending; c++) {
        list->stores[list->nr].ireg     = lazy_pending[c].ireg;
        list->stores[list->nr].host_reg = CODEGEN_LAZY_IMM;
        list->stores[list->nr].imm      = lazy_pending[c].imm;
        list->nr++;
    }
    reg_set = &host_reg_set;
    for (; deferred; deferred &= deferred - 1) {
        c = __builtin_ctz(deferred);
        list->stores[list->nr].ireg     = IREG_GET_REG(reg_set->regs[c].reg);
        list->stores[list->nr].host_reg = reg_set->reg_list[c].reg;
        list->stores[list->nr].imm      = 0;
        list->nr++;
    }
    if (list->nr) {
        nr_lazy_lists++;
        codegen_lazy_site = list;
    }
}
#endif

#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
/*The allocator at a branch over a cold region (UOP_TYPE_COLD): what each
  host register holds, whether it is dirty, and the immediates not stored
  yet. Regions do not nest, so there is one copy. The versions' read counts
  are not in it: after the region they count the reads the uops past it
  make, which is what the path that skips the region still has to make.*/
static struct {
    ir_reg_t regs[CODEGEN_HOST_REGS];
    uint8_t  dirty[CODEGEN_HOST_REGS];
    uint16_t slots[IREG_COUNT];
    ir_reg_t fp_regs[CODEGEN_HOST_FP_REGS];
    uint8_t  fp_dirty[CODEGEN_HOST_FP_REGS];
    uint16_t fp_slots[IREG_COUNT];
#    ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    struct {
        uint32_t imm;
        uint8_t  ireg;
    } lazy_pending[CODEGEN_LAZY_PENDING_MAX];
    int      nr_lazy_pending;
    uint64_t lazy_pending_mask[2];
#    endif
} cold_state;
#    ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
_Static_assert(sizeof(cold_state.lazy_pending) == sizeof(lazy_pending), "cold_state.lazy_pending is a copy of lazy_pending");
#    endif

void
codegen_reg_cold_snapshot(void)
{
    memcpy(cold_state.regs, host_reg_set.regs, sizeof(cold_state.regs));
    memcpy(cold_state.dirty, host_reg_set.dirty, sizeof(cold_state.dirty));
    memcpy(cold_state.slots, host_reg_set.slots, sizeof(cold_state.slots));
    memcpy(cold_state.fp_regs, host_fp_reg_set.regs, sizeof(cold_state.fp_regs));
    memcpy(cold_state.fp_dirty, host_fp_reg_set.dirty, sizeof(cold_state.fp_dirty));
    memcpy(cold_state.fp_slots, host_fp_reg_set.slots, sizeof(cold_state.fp_slots));
#    ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    memcpy(cold_state.lazy_pending, lazy_pending, sizeof(cold_state.lazy_pending));
    cold_state.nr_lazy_pending      = nr_lazy_pending;
    cold_state.lazy_pending_mask[0] = lazy_pending_mask[0];
    cold_state.lazy_pending_mask[1] = lazy_pending_mask[1];
#    endif
}

/*At the region's destination, which only the branch reaches: the host
  registers hold there what they held at the branch, which ran nothing
  else*/
void
codegen_reg_cold_restore(void)
{
    memcpy(host_reg_set.regs, cold_state.regs, sizeof(cold_state.regs));
    memcpy(host_reg_set.dirty, cold_state.dirty, sizeof(cold_state.dirty));
    memcpy(host_reg_set.slots, cold_state.slots, sizeof(cold_state.slots));
    memcpy(host_fp_reg_set.regs, cold_state.fp_regs, sizeof(cold_state.fp_regs));
    memcpy(host_fp_reg_set.dirty, cold_state.fp_dirty, sizeof(cold_state.fp_dirty));
    memcpy(host_fp_reg_set.slots, cold_state.fp_slots, sizeof(cold_state.fp_slots));
    host_reg_set.locked    = 0;
    host_fp_reg_set.locked = 0;
#    ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    memcpy(lazy_pending, cold_state.lazy_pending, sizeof(cold_state.lazy_pending));
    nr_lazy_pending      = cold_state.nr_lazy_pending;
    lazy_pending_mask[0] = cold_state.lazy_pending_mask[0];
    lazy_pending_mask[1] = cold_state.lazy_pending_mask[1];
    codegen_lazy_site    = NULL;
#    endif
}

/*Before the branch over a region that goes on at its destination, for each
  register the region writes: it leaves the host registers and the held
  immediates, stored first. After the region each path then finds its own
  value of it in memory, as when the branch wrote everything back.*/
void
codegen_reg_cold_drop(codeblock_t *block, int ireg)
{
    host_reg_set_t *reg_set = (ireg_data[ireg].type == REG_INTEGER) ? &host_reg_set : &host_fp_reg_set;

#    ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    lazy_pending_store_overlapping(block, ireg);
#    endif
    for (uint16_t slots = reg_set->slots[ireg]; slots; slots &= slots - 1) {
        int c = __builtin_ctz(slots);

        if (reg_set->dirty[c])
            codegen_reg_writeback(reg_set, block, c, 0);
        host_reg_assign(reg_set, c, invalid_ir_reg);
        reg_set->dirty[c] = 0;
    }
}

/*Whether the registers the allocator holds now can be loaded again from
  memory after a call: not when an IR register is in two host registers,
  two versions of which memory holds one*/
int
codegen_reg_cold_can_rejoin(void)
{
    for (int r = 0; r < IREG_COUNT; r++) {
        if ((host_reg_set.slots[r] & (host_reg_set.slots[r] - 1)) || (host_fp_reg_set.slots[r] & (host_fp_reg_set.slots[r] - 1)))
            return 0;
    }
    return 1;
}

/*The end of a region that goes on at its destination, after its call wrote
  every register back and dropped them: each register the copy holds is
  loaded into the host register it is in there (the call changes none of
  them in memory), and the copy is taken back. A temporary no uop reads any
  more is left out.*/
void
codegen_reg_cold_rejoin(codeblock_t *block)
{
#    ifndef RELEASE_BUILD
    if (nr_lazy_pending)
        fatal("codegen_reg_cold_rejoin - %i immediates held\n", nr_lazy_pending);
    for (int c = 0; c < CODEGEN_HOST_REGS; c++) {
        if (!ir_reg_is_invalid(host_reg_set.regs[c]) && host_reg_set.dirty[c] &&
            (!ireg_data[IREG_GET_REG(host_reg_set.regs[c].reg)].is_volatile || ir_get_refcount(host_reg_set.regs[c])))
            fatal("codegen_reg_cold_rejoin - %02x.%i not written back\n", host_reg_set.regs[c].reg, host_reg_set.regs[c].version);
    }
    for (int c = 0; c < CODEGEN_HOST_FP_REGS; c++) {
        if (!ir_reg_is_invalid(host_fp_reg_set.regs[c]) && host_fp_reg_set.dirty[c] &&
            (!ireg_data[IREG_GET_REG(host_fp_reg_set.regs[c].reg)].is_volatile || ir_get_refcount(host_fp_reg_set.regs[c])))
            fatal("codegen_reg_cold_rejoin - %02x.%i not written back\n", host_fp_reg_set.regs[c].reg, host_fp_reg_set.regs[c].version);
    }
#    endif
    for (int c = 0; c < CODEGEN_HOST_REGS; c++) {
        ir_reg_t ir_reg = cold_state.regs[c];

        if (ir_reg_is_invalid(ir_reg))
            continue;
        if (ireg_data[IREG_GET_REG(ir_reg.reg)].is_volatile && !ir_get_refcount(ir_reg)) {
            cold_state.slots[IREG_GET_REG(ir_reg.reg)] &= ~(1 << c);
            cold_state.regs[c]  = invalid_ir_reg;
            cold_state.dirty[c] = 0;
            continue;
        }
        codegen_reg_load(&host_reg_set, block, c, ir_reg);
    }
    for (int c = 0; c < CODEGEN_HOST_FP_REGS; c++) {
        ir_reg_t ir_reg = cold_state.fp_regs[c];

        if (ir_reg_is_invalid(ir_reg))
            continue;
        if (ireg_data[IREG_GET_REG(ir_reg.reg)].is_volatile && !ir_get_refcount(ir_reg)) {
            cold_state.fp_slots[IREG_GET_REG(ir_reg.reg)] &= ~(1 << c);
            cold_state.fp_regs[c]  = invalid_ir_reg;
            cold_state.fp_dirty[c] = 0;
            continue;
        }
        codegen_reg_load(&host_fp_reg_set, block, c, ir_reg);
    }
    codegen_reg_cold_restore();
}

#endif

void
codegen_reg_flush_invalidate(UNUSED(ir_data_t *ir), codeblock_t *block)
{
    host_reg_set_t *reg_set;
    int             c;

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
    lazy_pending_store_all(block);
#endif
    reg_set = &host_reg_set;
    for (c = 0; c < reg_set->nr_regs; c++) {
        if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c]) {
            codegen_reg_writeback(reg_set, block, c, 1);
        }
        host_reg_assign(reg_set, c, invalid_ir_reg);
        reg_set->dirty[c] = 0;
    }

    reg_set = &host_fp_reg_set;
    for (c = 0; c < reg_set->nr_regs; c++) {
        if (!ir_reg_is_invalid(reg_set->regs[c]) && reg_set->dirty[c]) {
            codegen_reg_writeback(reg_set, block, c, 1);
        }
        host_reg_assign(reg_set, c, invalid_ir_reg);
        reg_set->dirty[c] = 0;
    }
}

/*Process dead register list, and optimise out register versions and uOPs where
  possible*/
void
codegen_reg_process_dead_list(ir_data_t *ir)
{
    while (reg_dead_list) {
        int            version = reg_dead_list & 0xff;
        int            reg     = reg_dead_list >> 8;
        reg_version_t *regv    = &reg_version[reg][version];
        uop_t         *uop     = &ir->uops[regv->parent_uop];

        /*Barrier uOPs should be preserved*/
        if (!(uop->type & (UOP_TYPE_BARRIER | UOP_TYPE_ORDER_BARRIER))) {
            uop->type = UOP_INVALID;
            /*Adjust refcounts on source registers. If these drop to
              zero then those registers can be considered for removal*/
            if (uop->src_reg_a.reg != IREG_INVALID) {
                reg_version_t *src_regv = &reg_version[IREG_GET_REG(uop->src_reg_a.reg)][uop->src_reg_a.version];
                src_regv->refcount--;
                if (!src_regv->refcount)
                    add_to_dead_list(src_regv, IREG_GET_REG(uop->src_reg_a.reg), uop->src_reg_a.version);
            }
            if (uop->src_reg_b.reg != IREG_INVALID) {
                reg_version_t *src_regv = &reg_version[IREG_GET_REG(uop->src_reg_b.reg)][uop->src_reg_b.version];
                src_regv->refcount--;
                if (!src_regv->refcount)
                    add_to_dead_list(src_regv, IREG_GET_REG(uop->src_reg_b.reg), uop->src_reg_b.version);
            }
            if (uop->src_reg_c.reg != IREG_INVALID) {
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
