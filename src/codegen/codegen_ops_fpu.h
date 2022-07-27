static uint32_t ropFXCH(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();

        FP_FXCH(opcode & 7);

        return op_pc;
}

static uint32_t ropFLD(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();

        FP_FLD(opcode & 7);

        return op_pc;
}

static uint32_t ropFST(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();

        FP_FST(opcode & 7);

        return op_pc;
}
static uint32_t ropFSTP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();

        FP_FST(opcode & 7);
        FP_POP();

        return op_pc;
}


static uint32_t ropFLDs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_L(target_seg);

        FP_LOAD_S();

        return op_pc + 1;
}
static uint32_t ropFLDd(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_Q(target_seg);

        FP_LOAD_D();

        return op_pc + 1;
}

static uint32_t ropFILDw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_W(target_seg);

        FP_LOAD_IW();

        return op_pc + 1;
}
static uint32_t ropFILDl(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_L(target_seg);

        FP_LOAD_IL();

        return op_pc + 1;
}
static uint32_t ropFILDq(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_Q(target_seg);

        FP_LOAD_IQ();

        codegen_fpu_loaded_iq[(cpu_state.TOP - 1) & 7] = 1;

        return op_pc + 1;
}

static uint32_t ropFSTs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;
        int host_reg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        host_reg = FP_LOAD_REG(0);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_WRITE(target_seg);

        MEM_STORE_ADDR_EA_L(target_seg, host_reg);

        return op_pc + 1;
}
static uint32_t ropFSTd(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;
        int host_reg1, host_reg2 = 0;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        FP_LOAD_REG_D(0, &host_reg1, &host_reg2);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_WRITE(target_seg);
        CHECK_SEG_LIMITS(target_seg, 7);

        MEM_STORE_ADDR_EA_Q(target_seg, host_reg1, host_reg2);

        return op_pc + 1;
}

static uint32_t ropFSTPs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        uint32_t new_pc = ropFSTs(opcode, fetchdat, op_32, op_pc, block);

        FP_POP();

        return new_pc;
}
static uint32_t ropFSTPd(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        uint32_t new_pc = ropFSTd(opcode, fetchdat, op_32, op_pc, block);

        FP_POP();

        return new_pc;
}

#define ropFarith(name, size, load, op) \
static uint32_t ropF ## name ## size(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)     \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        FP_ENTER();                                                                                                             \
        op_pc--;                                                                                                                \
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                                                    \
                                                                                                                                \
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);                                                                         \
                                                                                                                                \
        CHECK_SEG_READ(target_seg);                                                                                             \
        load(target_seg);                                                                                                       \
                                                                                                                                \
        op(FPU_ ## name);                                                                                                       \
                                                                                                                                \
        return op_pc + 1;                                                                                                       \
}

ropFarith(ADD,  s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(DIV,  s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(DIVR, s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(MUL,  s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(SUB,  s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(SUBR, s, MEM_LOAD_ADDR_EA_L, FP_OP_S);
ropFarith(ADD,  d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(DIV,  d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(DIVR, d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(MUL,  d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(SUB,  d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(SUBR, d, MEM_LOAD_ADDR_EA_Q, FP_OP_D);
ropFarith(ADD,  iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(DIV,  iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(DIVR, iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(MUL,  iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(SUB,  iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(SUBR, iw, MEM_LOAD_ADDR_EA_W, FP_OP_IW);
ropFarith(ADD,  il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);
ropFarith(DIV,  il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);
ropFarith(DIVR, il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);
ropFarith(MUL,  il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);
ropFarith(SUB,  il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);
ropFarith(SUBR, il, MEM_LOAD_ADDR_EA_L, FP_OP_IL);

#define ropFcompare(name, size, load, op) \
static uint32_t ropF ## name ## size(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)     \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        FP_ENTER();                                                                                                             \
        op_pc--;                                                                                                                \
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                                                    \
                                                                                                                                \
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);                                                                         \
                                                                                                                                \
        CHECK_SEG_READ(target_seg);                                                                                             \
        load(target_seg);                                                                                                       \
                                                                                                                                \
        op();                                                                                                                   \
                                                                                                                                \
        return op_pc + 1;                                                                                                       \
}                                                                                                                               \
static uint32_t ropF ## name ## P ## size(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)        \
{                                                                                                                               \
        uint32_t new_pc = ropF ## name ## size(opcode, fetchdat, op_32, op_pc, block);                                           \
                                                                                                                                \
        FP_POP();                                                                                                               \
                                                                                                                                \
        return new_pc;                                                                                                          \
}

ropFcompare(COM, s, MEM_LOAD_ADDR_EA_L, FP_COMPARE_S);
ropFcompare(COM, d, MEM_LOAD_ADDR_EA_Q, FP_COMPARE_D);
ropFcompare(COM, iw, MEM_LOAD_ADDR_EA_W, FP_COMPARE_IW);
ropFcompare(COM, il, MEM_LOAD_ADDR_EA_L, FP_COMPARE_IL);

/*static uint32_t ropFADDs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_L(target_seg);

        FP_OP_S(FPU_ADD);

        return op_pc + 1;
}
static uint32_t ropFDIVs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_L(target_seg);

        FP_OP_S(FPU_DIV);

        return op_pc + 1;
}
static uint32_t ropFMULs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_L(target_seg);

        FP_OP_S(FPU_MUL);

        return op_pc + 1;
}
static uint32_t ropFSUBs(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_L(target_seg);

        FP_OP_S(FPU_SUB);

        return op_pc + 1;
}*/


static uint32_t ropFADD(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_ADD, 0, opcode & 7);

        return op_pc;
}
static uint32_t ropFCOM(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_COMPARE_REG(0, opcode & 7);

        return op_pc;
}
static uint32_t ropFDIV(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_DIV, 0, opcode & 7);

        return op_pc;
}
static uint32_t ropFDIVR(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_DIVR, 0, opcode & 7);

        return op_pc;
}
static uint32_t ropFMUL(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_MUL, 0, opcode & 7);

        return op_pc;
}
static uint32_t ropFSUB(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_SUB, 0, opcode & 7);

        return op_pc;
}
static uint32_t ropFSUBR(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_SUBR, 0, opcode & 7);

        return op_pc;
}

static uint32_t ropFADDr(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_ADD, opcode & 7, 0);

        return op_pc;
}
static uint32_t ropFDIVr(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_DIV, opcode & 7, 0);

        return op_pc;
}
static uint32_t ropFDIVRr(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_DIVR, opcode & 7, 0);

        return op_pc;
}
static uint32_t ropFMULr(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_MUL, opcode & 7, 0);

        return op_pc;
}
static uint32_t ropFSUBr(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_SUB, opcode & 7, 0);

        return op_pc;
}
static uint32_t ropFSUBRr(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_SUBR, opcode & 7, 0);

        return op_pc;
}

static uint32_t ropFADDP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_ADD, opcode & 7, 0);
        FP_POP();

        return op_pc;
}
static uint32_t ropFCOMP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_COMPARE_REG(0, opcode & 7);
        FP_POP();

        return op_pc;
}
static uint32_t ropFDIVP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_DIV, opcode & 7, 0);
        FP_POP();

        return op_pc;
}
static uint32_t ropFDIVRP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_DIVR, opcode & 7, 0);
        FP_POP();

        return op_pc;
}
static uint32_t ropFMULP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_MUL, opcode & 7, 0);
        FP_POP();

        return op_pc;
}
static uint32_t ropFSUBP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_SUB, opcode & 7, 0);
        FP_POP();

        return op_pc;
}
static uint32_t ropFSUBRP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_OP_REG(FPU_SUBR, opcode & 7, 0);
        FP_POP();

        return op_pc;
}

static uint32_t ropFCOMPP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_COMPARE_REG(0, 1);
        FP_POP2();

        return op_pc;
}

static uint32_t ropFSTSW_AX(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;

        FP_ENTER();
        host_reg = LOAD_VAR_W((uintptr_t)&cpu_state.npxs);
        STORE_REG_TARGET_W_RELEASE(host_reg, REG_AX);

        return op_pc;
}


static uint32_t ropFISTw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;
        int host_reg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        host_reg = FP_LOAD_REG_INT_W(0);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_WRITE(target_seg);

        MEM_STORE_ADDR_EA_W(target_seg, host_reg);

        return op_pc + 1;
}
static uint32_t ropFISTl(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;
        int host_reg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        host_reg = FP_LOAD_REG_INT(0);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_WRITE(target_seg);

        MEM_STORE_ADDR_EA_L(target_seg, host_reg);

        return op_pc + 1;
}

static uint32_t ropFISTPw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        uint32_t new_pc = ropFISTw(opcode, fetchdat, op_32, op_pc, block);

        FP_POP();

        return new_pc;
}
static uint32_t ropFISTPl(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        uint32_t new_pc = ropFISTl(opcode, fetchdat, op_32, op_pc, block);

        FP_POP();

        return new_pc;
}
static uint32_t ropFISTPq(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;
        int host_reg1, host_reg2;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        FP_LOAD_REG_INT_Q(0, &host_reg1, &host_reg2);

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

        CHECK_SEG_WRITE(target_seg);

        MEM_STORE_ADDR_EA_Q(target_seg, host_reg1, host_reg2);

        FP_POP();

        return op_pc + 1;
}


static uint32_t ropFLDCW(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_W(target_seg);
        STORE_HOST_REG_ADDR_W((uintptr_t)&cpu_state.npxc, 0);
        UPDATE_NPXC(0);

        return op_pc + 1;
}
static uint32_t ropFSTCW(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;
        x86seg *target_seg;

        FP_ENTER();
        op_pc--;
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        CHECK_SEG_WRITE(target_seg);

        host_reg = LOAD_VAR_W((uintptr_t)&cpu_state.npxc);
        MEM_STORE_ADDR_EA_W(target_seg, host_reg);

        return op_pc + 1;
}


static uint32_t ropFCHS(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_FCHS();

        return op_pc;
}

#define opFLDimm(name, v)                                                                                                       \
        static uint32_t ropFLD ## name(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)   \
        {                                                                                                                       \
                static double fp_imm = v;                                                                                       \
                                                                                                                                \
                FP_ENTER();                                                                                                     \
                FP_LOAD_IMM_Q(*(uint64_t *)&fp_imm);                                                                            \
                                                                                                                                \
                return op_pc;                                                                                                   \
        }

opFLDimm(1, 1.0)
opFLDimm(L2T, 3.3219280948873623)
opFLDimm(L2E, 1.4426950408889634);
opFLDimm(PI, 3.141592653589793);
opFLDimm(EG2, 0.3010299956639812);
opFLDimm(Z, 0.0)

static uint32_t ropFLDLN2(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        FP_ENTER();
        FP_LOAD_IMM_Q(0x3fe62e42fefa39f0ull);

        return op_pc;
}
