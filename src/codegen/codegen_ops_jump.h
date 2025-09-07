static uint32_t
ropJMP_r8(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t offset = fetchdat & 0xff;

    if (offset & 0x80)
        offset |= 0xffffff00;

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.pc, op_pc + 1 + offset);

    return -1;
}

static uint32_t
ropJMP_r16(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint16_t offset = fetchdat & 0xffff;

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.pc, (op_pc + 2 + offset) & 0xffff);

    return -1;
}

static uint32_t
ropJMP_r32(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t offset = fastreadl(cs + op_pc);

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.pc, op_pc + 4 + offset);

    return -1;
}

static uint32_t
ropJCXZ(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t offset = fetchdat & 0xff;

    if (offset & 0x80)
        offset |= 0xffffff00;

    if (op_32 & 0x200) {
        int host_reg = LOAD_REG_L(REG_ECX);
        TEST_ZERO_JUMP_L(host_reg, op_pc + 1 + offset, 0);
    } else {
        int host_reg = LOAD_REG_W(REG_CX);
        TEST_ZERO_JUMP_W(host_reg, op_pc + 1 + offset, 0);
    }

    return op_pc + 1;
}

static uint32_t
ropLOOP(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t offset = fetchdat & 0xff;

    if (offset & 0x80)
        offset |= 0xffffff00;

    if (op_32 & 0x200) {
        int host_reg = LOAD_REG_L(REG_ECX);
        SUB_HOST_REG_IMM(host_reg, 1);
        STORE_REG_L_RELEASE(host_reg);
        TEST_NONZERO_JUMP_L(host_reg, op_pc + 1 + offset, 0);
    } else {
        int host_reg = LOAD_REG_W(REG_CX);
        SUB_HOST_REG_IMM(host_reg, 1);
        STORE_REG_W_RELEASE(host_reg);
        TEST_NONZERO_JUMP_W(host_reg, op_pc + 1 + offset, 0);
    }

    return op_pc + 1;
}

static void
BRANCH_COND_B(int pc_offset, uint32_t op_pc, uint32_t offset, int not )
{
    CALL_FUNC((uintptr_t) CF_SET);
    if (not )
        TEST_ZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
    else
        TEST_NONZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
}

static void
BRANCH_COND_E(int pc_offset, uint32_t op_pc, uint32_t offset, int not )
{
    int host_reg;

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
        case FLAGS_ADD8:
        case FLAGS_ADD16:
        case FLAGS_ADD32:
        case FLAGS_SUB8:
        case FLAGS_SUB16:
        case FLAGS_SUB32:
        case FLAGS_SHL8:
        case FLAGS_SHL16:
        case FLAGS_SHL32:
        case FLAGS_SHR8:
        case FLAGS_SHR16:
        case FLAGS_SHR32:
        case FLAGS_SAR8:
        case FLAGS_SAR16:
        case FLAGS_SAR32:
        case FLAGS_INC8:
        case FLAGS_INC16:
        case FLAGS_INC32:
        case FLAGS_DEC8:
        case FLAGS_DEC16:
        case FLAGS_DEC32:
            host_reg = LOAD_VAR_L((uintptr_t) &cpu_state.flags_res);
            if (not )
                TEST_NONZERO_JUMP_L(host_reg, op_pc + pc_offset + offset, timing_bt);
            else
                TEST_ZERO_JUMP_L(host_reg, op_pc + pc_offset + offset, timing_bt);
            break;

        case FLAGS_UNKNOWN:
            CALL_FUNC((uintptr_t) ZF_SET);
            if (not )
                TEST_ZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
            else
                TEST_NONZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
            break;
    }
}

static void
BRANCH_COND_O(int pc_offset, uint32_t op_pc, uint32_t offset, int not )
{
    CALL_FUNC((uintptr_t) VF_SET);
    if (not )
        TEST_ZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
    else
        TEST_NONZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
}

static void
BRANCH_COND_P(int pc_offset, uint32_t op_pc, uint32_t offset, int not )
{
    CALL_FUNC((uintptr_t) PF_SET);
    if (not )
        TEST_ZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
    else
        TEST_NONZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
}

static void
BRANCH_COND_S(int pc_offset, uint32_t op_pc, uint32_t offset, int not )
{
    int host_reg;

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ADD8:
        case FLAGS_SUB8:
        case FLAGS_SHL8:
        case FLAGS_SHR8:
        case FLAGS_SAR8:
        case FLAGS_INC8:
        case FLAGS_DEC8:
            host_reg = LOAD_VAR_L((uintptr_t) &cpu_state.flags_res);
            AND_HOST_REG_IMM(host_reg, 0x80);
            if (not )
                TEST_ZERO_JUMP_L(host_reg, op_pc + pc_offset + offset, timing_bt);
            else
                TEST_NONZERO_JUMP_L(host_reg, op_pc + pc_offset + offset, timing_bt);
            break;

        case FLAGS_ZN16:
        case FLAGS_ADD16:
        case FLAGS_SUB16:
        case FLAGS_SHL16:
        case FLAGS_SHR16:
        case FLAGS_SAR16:
        case FLAGS_INC16:
        case FLAGS_DEC16:
            host_reg = LOAD_VAR_L((uintptr_t) &cpu_state.flags_res);
            AND_HOST_REG_IMM(host_reg, 0x8000);
            if (not )
                TEST_ZERO_JUMP_L(host_reg, op_pc + pc_offset + offset, timing_bt);
            else
                TEST_NONZERO_JUMP_L(host_reg, op_pc + pc_offset + offset, timing_bt);
            break;

        case FLAGS_ZN32:
        case FLAGS_ADD32:
        case FLAGS_SUB32:
        case FLAGS_SHL32:
        case FLAGS_SHR32:
        case FLAGS_SAR32:
        case FLAGS_INC32:
        case FLAGS_DEC32:
            host_reg = LOAD_VAR_L((uintptr_t) &cpu_state.flags_res);
            AND_HOST_REG_IMM(host_reg, 0x80000000);
            if (not )
                TEST_ZERO_JUMP_L(host_reg, op_pc + pc_offset + offset, timing_bt);
            else
                TEST_NONZERO_JUMP_L(host_reg, op_pc + pc_offset + offset, timing_bt);
            break;

        case FLAGS_UNKNOWN:
            CALL_FUNC((uintptr_t) NF_SET);
            if (not )
                TEST_ZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
            else
                TEST_NONZERO_JUMP_L(0, op_pc + pc_offset + offset, timing_bt);
            break;
    }
}

#define ropBRANCH(name, func, not )              \
    static uint32_t                              \
    rop##name(UNUSED(uint8_t opcode),            \
              uint32_t fetchdat,                 \
              UNUSED(uint32_t op_32),            \
              uint32_t op_pc,                    \
              UNUSED(codeblock_t *block))        \
    {                                            \
        uint32_t offset = fetchdat & 0xff;       \
                                                 \
        if (offset & 0x80)                       \
            offset |= 0xffffff00;                \
                                                 \
        func(1, op_pc, offset, not );            \
                                                 \
        return op_pc + 1;                        \
    }                                            \
    static uint32_t                              \
    rop##name##_w(UNUSED(uint8_t opcode),        \
                  uint32_t fetchdat,             \
                  UNUSED(uint32_t op_32),        \
                  uint32_t op_pc,                \
                  UNUSED(codeblock_t *block))    \
    {                                            \
        uint32_t offset = fetchdat & 0xffff;     \
                                                 \
        if (offset & 0x8000)                     \
            offset |= 0xffff0000;                \
                                                 \
        func(2, op_pc, offset, not );            \
                                                 \
        return op_pc + 2;                        \
    }                                            \
    static uint32_t                              \
    rop##name##_l(UNUSED(uint8_t opcode),        \
                  UNUSED(uint32_t fetchdat),     \
                  UNUSED(uint32_t op_32),        \
                  uint32_t op_pc,                \
                  UNUSED(codeblock_t *block))    \
    {                                            \
        uint32_t offset = fastreadl(cs + op_pc); \
                                                 \
        func(4, op_pc, offset, not );            \
                                                 \
        return op_pc + 4;                        \
    }

// clang-format off
ropBRANCH(JB,   BRANCH_COND_B, 0)
ropBRANCH(JNB,  BRANCH_COND_B, 1)
ropBRANCH(JE,   BRANCH_COND_E, 0)
ropBRANCH(JNE,  BRANCH_COND_E, 1)
ropBRANCH(JO,   BRANCH_COND_O, 0)
ropBRANCH(JNO,  BRANCH_COND_O, 1)
ropBRANCH(JP,   BRANCH_COND_P, 0)
ropBRANCH(JNP,  BRANCH_COND_P, 1)
ropBRANCH(JS,   BRANCH_COND_S, 0)
ropBRANCH(JNS,  BRANCH_COND_S, 1)
ropBRANCH(JL,   BRANCH_COND_L, 0)
ropBRANCH(JNL,  BRANCH_COND_L, 1)
ropBRANCH(JLE,  BRANCH_COND_LE, 0)
ropBRANCH(JNLE, BRANCH_COND_LE, 1)
ropBRANCH(JBE,  BRANCH_COND_BE, 0)
ropBRANCH(JNBE, BRANCH_COND_BE, 1)
// clang-format on
