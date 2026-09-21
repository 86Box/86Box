#define SHIFT(size, size2, res_store, immediate)                                 \
    if ((fetchdat & 0xc0) == 0xc0) {                                             \
        reg = LOAD_REG_##size(fetchdat & 7);                                     \
        if (immediate)                                                           \
            count = (fetchdat >> 8) & 0x1f;                                      \
    } else {                                                                     \
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);     \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);               \
        SAVE_EA();                                                               \
        MEM_CHECK_WRITE_##size(target_seg);                                      \
        reg = MEM_LOAD_ADDR_EA_##size##_NO_ABRT(target_seg);                     \
        if (immediate)                                                           \
            count = fastreadb(cs + op_pc + 1) & 0x1f;                            \
    }                                                                            \
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op2, count);                   \
                                                                                 \
    res_store((uintptr_t) &cpu_state.flags_op1, reg);                            \
                                                                                 \
    switch (fetchdat & 0x38) {                                                   \
        case 0x20:                                                               \
        case 0x30: /*SHL*/                                                       \
            SHL_##size##_IMM(reg, count);                                        \
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_SHL##size2); \
            break;                                                               \
                                                                                 \
        case 0x28: /*SHR*/                                                       \
            SHR_##size##_IMM(reg, count);                                        \
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_SHR##size2); \
            break;                                                               \
                                                                                 \
        case 0x38: /*SAR*/                                                       \
            SAR_##size##_IMM(reg, count);                                        \
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_SAR##size2); \
            break;                                                               \
    }                                                                            \
                                                                                 \
    res_store((uintptr_t) &cpu_state.flags_res, reg);                            \
    if ((fetchdat & 0xc0) == 0xc0)                                               \
        STORE_REG_##size##_RELEASE(reg);                                         \
    else {                                                                       \
        LOAD_EA();                                                               \
        MEM_STORE_ADDR_EA_##size##_NO_ABRT(target_seg, reg);                     \
    }

static uint32_t
ropC0(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg = NULL;
    int     count;
    int     reg;

    if ((fetchdat & 0x38) < 0x20)
        return 0;

    SHIFT(B, 8, STORE_HOST_REG_ADDR_BL, 1);

    return op_pc + 2;
}
static uint32_t
ropC1_w(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg = NULL;
    int     count;
    int     reg;

    if ((fetchdat & 0x38) < 0x20)
        return 0;

    SHIFT(W, 16, STORE_HOST_REG_ADDR_WL, 1);

    return op_pc + 2;
}
static uint32_t
ropC1_l(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg = NULL;
    int     count;
    int     reg;

    if ((fetchdat & 0x38) < 0x20)
        return 0;

    SHIFT(L, 32, STORE_HOST_REG_ADDR, 1);

    return op_pc + 2;
}

static uint32_t
ropD0(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg = NULL;
    int     count      = 1;
    int     reg;

    if ((fetchdat & 0x38) < 0x20)
        return 0;

    SHIFT(B, 8, STORE_HOST_REG_ADDR_BL, 0);

    return op_pc + 1;
}
static uint32_t
ropD1_w(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg = NULL;
    int     count      = 1;
    int     reg;

    if ((fetchdat & 0x38) < 0x20)
        return 0;

    SHIFT(W, 16, STORE_HOST_REG_ADDR_WL, 0);

    return op_pc + 1;
}
static uint32_t
ropD1_l(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg = NULL;
    int     count      = 1;
    int     reg;

    if ((fetchdat & 0x38) < 0x20)
        return 0;

    SHIFT(L, 32, STORE_HOST_REG_ADDR, 0);

    return op_pc + 1;
}
