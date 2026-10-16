static uint32_t
ropFXCH(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();

    FP_FXCH(opcode & 7);

    return op_pc;
}

static uint32_t
ropFLD(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();

    FP_FLD(opcode & 7);

    return op_pc;
}

static uint32_t
ropFST(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int done_pos;

    FP_ENTER();

    done_pos = FP_EMPTY_CHECK((opcode & 7) << 16, NULL);
    FP_FST(opcode & 7);
    FP_EMPTY_DONE(done_pos);

    return op_pc;
}
static uint32_t
ropFSTP(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int done_pos;

    FP_ENTER();

    done_pos = FP_EMPTY_CHECK(((opcode & 7) << 16) | 0x100, NULL);
    FP_FST(opcode & 7);
    FP_POP();
    FP_EMPTY_DONE(done_pos);

    return op_pc;
}

static uint32_t
ropFLDs(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    CHECK_SEG_READ_EA(target_seg, 4);
    MEM_LOAD_ADDR_EA_L(target_seg);

    FP_LOAD_S();

    return op_pc + 1;
}
static uint32_t
ropFLDd(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    CHECK_SEG_READ_EA(target_seg, 8);
    MEM_LOAD_ADDR_EA_Q(target_seg);

    FP_LOAD_D();

    return op_pc + 1;
}

static uint32_t
ropFILDw(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    CHECK_SEG_READ_EA(target_seg, 2);
    MEM_LOAD_ADDR_EA_W(target_seg);

    FP_LOAD_IW();

    return op_pc + 1;
}
static uint32_t
ropFILDl(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    CHECK_SEG_READ_EA(target_seg, 4);
    MEM_LOAD_ADDR_EA_L(target_seg);

    FP_LOAD_IL();

    return op_pc + 1;
}
static uint32_t
ropFILDq(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    CHECK_SEG_READ_EA(target_seg, 8);
    MEM_LOAD_ADDR_EA_Q(target_seg);

    FP_LOAD_IQ();

    codegen_fpu_loaded_iq[(cpu_state.TOP - 1) & 7] = 1;

    return op_pc + 1;
}

/*FST and FSTP m32/m64: from an empty ST(0) the interpreter's stack fault
  runs instead (FP_EMPTY_CHECK)*/
static uint32_t
ropFST_mem(uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int size, int pop)
{
    x86seg *target_seg;
    int     host_reg1;
    int     host_reg2 = 0;
    int     done_pos;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    CHECK_SEG_WRITE_EA(target_seg, size);

    done_pos = FP_EMPTY_CHECK(size | (pop ? 0x100 : 0), target_seg);
    if (size == 4) {
        host_reg1 = FP_LOAD_REG(0);
        MEM_STORE_ADDR_EA_L(target_seg, host_reg1);
    } else {
        FP_LOAD_REG_D(0, &host_reg1, &host_reg2);
        MEM_STORE_ADDR_EA_Q(target_seg, host_reg1, host_reg2);
    }
    if (pop)
        FP_POP();
    FP_EMPTY_DONE(done_pos);

    return op_pc + 1;
}
static uint32_t
ropFSTs(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    return ropFST_mem(fetchdat, op_32, op_pc, 4, 0);
}
static uint32_t
ropFSTd(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    return ropFST_mem(fetchdat, op_32, op_pc, 8, 0);
}
static uint32_t
ropFSTPs(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    return ropFST_mem(fetchdat, op_32, op_pc, 4, 1);
}
static uint32_t
ropFSTPd(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    return ropFST_mem(fetchdat, op_32, op_pc, 8, 1);
}

/*The memory operand's size, from the name's suffix*/
#define FP_BYTES_s  4
#define FP_BYTES_d  8
#define FP_BYTES_iw 2
#define FP_BYTES_il 4

#define ropFarith(name, size, load, op)                                      \
    static uint32_t                                                          \
    ropF##name##size(UNUSED(uint8_t opcode),                                 \
                     uint32_t fetchdat,                                      \
                     uint32_t op_32,                                         \
                     uint32_t op_pc,                                         \
                     UNUSED(codeblock_t *block))                             \
    {                                                                        \
        x86seg *target_seg;                                                  \
                                                                             \
        /*The interpreter rounds an FADD from memory by RC*/                 \
        if ((FPU_##name == FPU_ADD) && ((cpu_state.npxc >> 10) & 3))         \
            return 0;                                                        \
                                                                             \
        FP_ENTER();                                                          \
        op_pc--;                                                             \
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32); \
                                                                             \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);           \
                                                                             \
        CHECK_SEG_READ_EA(target_seg, FP_BYTES_##size);                      \
        load(target_seg);                                                    \
                                                                             \
        op(FPU_##name);                                                      \
                                                                             \
        return op_pc + 1;                                                    \
    }

ropFarith(ADD, s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(MUL, s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(SUB, s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(SUBR, s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(ADD, d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(MUL, d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(SUB, d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(SUBR, d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(ADD, iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(MUL, iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(SUB, iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(SUBR, iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(ADD, il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);
ropFarith(MUL, il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);
ropFarith(SUB, il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);
ropFarith(SUBR, il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);

#define ropFcompare(name, size, load, op)                                          \
    static uint32_t                                                                \
    ropF##name##size(UNUSED(uint8_t opcode),                                       \
                     uint32_t fetchdat,                                            \
                     uint32_t op_32,                                               \
                     uint32_t op_pc,                                               \
                     UNUSED(codeblock_t *block))                                   \
    {                                                                              \
        x86seg *target_seg;                                                        \
                                                                                   \
        FP_ENTER();                                                                \
        op_pc--;                                                                   \
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);       \
                                                                                   \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);                 \
                                                                                   \
        CHECK_SEG_READ_EA(target_seg, FP_BYTES_##size);                            \
        load(target_seg);                                                          \
                                                                                   \
        op();                                                                      \
                                                                                   \
        return op_pc + 1;                                                          \
    }                                                                              \
    static uint32_t ropF##name##P##size(uint8_t opcode,                            \
                                        uint32_t fetchdat,                         \
                                        uint32_t op_32,                            \
                                        uint32_t op_pc,                            \
                                        codeblock_t *block)                        \
    {                                                                              \
        uint32_t new_pc = ropF##name##size(opcode, fetchdat, op_32, op_pc, block); \
                                                                                   \
        FP_POP();                                                                  \
                                                                                   \
        return new_pc;                                                             \
    }

ropFcompare(COM, s, MEM_LOAD_ADDR_EA_L, FP_COMPARE_S);
ropFcompare(COM, d, MEM_LOAD_ADDR_EA_Q, FP_COMPARE_D);
ropFcompare(COM, iw, MEM_LOAD_ADDR_EA_W, FP_COMPARE_IW);
ropFcompare(COM, il, MEM_LOAD_ADDR_EA_L, FP_COMPARE_IL);

#if 0
static uint32_t
ropFADDs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

    CHECK_SEG_READ_EA(target_seg, 4);
    MEM_LOAD_ADDR_EA_L(target_seg);

    FP_OP_S(FPU_ADD);

    return op_pc + 1;
}
static uint32_t
ropFDIVs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

    CHECK_SEG_READ_EA(target_seg, 4);
    MEM_LOAD_ADDR_EA_L(target_seg);

    FP_OP_S(FPU_DIV);

    return op_pc + 1;
}
static uint32_t
ropFMULs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

    CHECK_SEG_READ_EA(target_seg, 4);
    MEM_LOAD_ADDR_EA_L(target_seg);

    FP_OP_S(FPU_MUL);

    return op_pc + 1;
}
static uint32_t
ropFSUBs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

    CHECK_SEG_READ_EA(target_seg, 4);
    MEM_LOAD_ADDR_EA_L(target_seg);

    FP_OP_S(FPU_SUB);

    return op_pc + 1;
}
#endif

static uint32_t
ropFADD(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_ADD, 0, opcode & 7);

    return op_pc;
}
static uint32_t
ropFCOM(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_COMPARE_REG(0, opcode & 7);

    return op_pc;
}
static uint32_t
ropFMUL(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_MUL, 0, opcode & 7);

    return op_pc;
}
static uint32_t
ropFSUB(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_SUB, 0, opcode & 7);

    return op_pc;
}
static uint32_t
ropFSUBR(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_SUBR, 0, opcode & 7);

    return op_pc;
}

static uint32_t
ropFADDr(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_ADD, opcode & 7, 0);

    return op_pc;
}
static uint32_t
ropFMULr(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_MUL, opcode & 7, 0);

    return op_pc;
}
static uint32_t
ropFSUBr(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_SUB, opcode & 7, 0);

    return op_pc;
}
static uint32_t
ropFSUBRr(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_SUBR, opcode & 7, 0);

    return op_pc;
}

static uint32_t
ropFADDP(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_ADD, opcode & 7, 0);
    FP_POP();

    return op_pc;
}
static uint32_t
ropFCOMP(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_COMPARE_REG(0, opcode & 7);
    FP_POP();

    return op_pc;
}
static uint32_t
ropFMULP(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_MUL, opcode & 7, 0);
    FP_POP();

    return op_pc;
}
static uint32_t
ropFSUBP(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_SUB, opcode & 7, 0);
    FP_POP();

    return op_pc;
}
static uint32_t
ropFSUBRP(uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_OP_REG(FPU_SUBR, opcode & 7, 0);
    FP_POP();

    return op_pc;
}

static uint32_t
ropFSTSW_AX(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg;

    FP_ENTER();
    host_reg = LOAD_VAR_W((uintptr_t) &cpu_state.npxs);
    /*The status word has TOP in bits 11-13*/
    addbyte(0x81); /*AND EBX, 0xc7ff*/
    addbyte(0xe3);
    addlong(0xc7ff);
    addbyte(0x8b); /*MOV ECX, [TOP]*/
    addbyte(0x4d);
    addbyte((uint8_t) cpu_state_offset(TOP));
    addbyte(0x83); /*AND ECX, 7*/
    addbyte(0xe1);
    addbyte(7);
    addbyte(0xc1); /*SHL ECX, 11*/
    addbyte(0xe1);
    addbyte(11);
    addbyte(0x09); /*OR EBX, ECX*/
    addbyte(0xcb);
    STORE_REG_TARGET_W_RELEASE(host_reg, REG_AX);

    return op_pc;
}

static uint32_t
ropFSTCW(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int     host_reg;
    x86seg *target_seg;

    FP_ENTER();
    op_pc--;
    target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    CHECK_SEG_WRITE_EA(target_seg, 2);

    host_reg = LOAD_VAR_W((uintptr_t) &cpu_state.npxc);
    MEM_STORE_ADDR_EA_W(target_seg, host_reg);

    return op_pc + 1;
}

static uint32_t
ropFCHS(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_FCHS();

    return op_pc;
}

#define opFLDimm(name, v)                             \
    static uint32_t                                   \
    ropFLD##name(UNUSED(uint8_t opcode),              \
                 UNUSED(uint32_t fetchdat),           \
                 UNUSED(uint32_t op_32),              \
                 uint32_t op_pc,                      \
                 UNUSED(codeblock_t *block))          \
    {                                                 \
        static double fp_imm = v;                     \
        static uint64_t *fptr = (uint64_t *) &fp_imm; \
                                                      \
        FP_ENTER();                                   \
        FP_LOAD_IMM_Q(*fptr);                         \
                                                      \
        return op_pc;                                 \
    }

// clang-format off
opFLDimm(1, 1.0)
opFLDimm(L2T, 3.3219280948873623)
opFLDimm(L2E, 1.4426950408889634);
opFLDimm(PI, 3.141592653589793);
opFLDimm(EG2, 0.3010299956639812);
opFLDimm(Z, 0.0)
// clang-format on

static uint32_t
ropFLDLN2(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    FP_ENTER();
    FP_LOAD_IMM_Q(0x3fe62e42fefa39f0ULL);

    return op_pc;
}
