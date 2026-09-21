#define ROP_LOGIC(name, op, writeback)                                                                       \
    static uint32_t                                                                                          \
    rop##name##_b_rmw(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block)) \
    {                                                                                                        \
        int     src_reg;                                                                                     \
        int     dst_reg;                                                                                     \
        x86seg *target_seg;                                                                                  \
                                                                                                             \
        if ((fetchdat & 0xc0) == 0xc0) {                                                                     \
            dst_reg = LOAD_REG_B(fetchdat & 7);                                                              \
        } else {                                                                                             \
            target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                             \
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);                                       \
            SAVE_EA();                                                                                       \
            MEM_CHECK_WRITE(target_seg);                                                                     \
            dst_reg = MEM_LOAD_ADDR_EA_B_NO_ABRT(target_seg);                                                \
        }                                                                                                    \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN8);                                        \
        src_reg = LOAD_REG_B((fetchdat >> 3) & 7);                                                           \
        op##_HOST_REG_B(dst_reg, src_reg);                                                                   \
        STORE_HOST_REG_ADDR_BL((uintptr_t) &cpu_state.flags_res, dst_reg);                                   \
        if (writeback) {                                                                                     \
            if ((fetchdat & 0xc0) == 0xc0)                                                                   \
                STORE_REG_B_RELEASE(dst_reg);                                                                \
            else {                                                                                           \
                LOAD_EA();                                                                                   \
                MEM_STORE_ADDR_EA_B_NO_ABRT(target_seg, dst_reg);                                            \
            }                                                                                                \
        } else                                                                                               \
            RELEASE_REG(dst_reg);                                                                            \
        RELEASE_REG(src_reg);                                                                                \
                                                                                                             \
        return op_pc + 1;                                                                                    \
    }                                                                                                        \
    static uint32_t                                                                                          \
    rop##name##_w_rmw(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block)) \
    {                                                                                                        \
        int     src_reg;                                                                                     \
        int     dst_reg;                                                                                     \
        x86seg *target_seg;                                                                                  \
                                                                                                             \
        if ((fetchdat & 0xc0) == 0xc0) {                                                                     \
            dst_reg = LOAD_REG_W(fetchdat & 7);                                                              \
        } else {                                                                                             \
            target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                             \
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);                                       \
            SAVE_EA();                                                                                       \
            MEM_CHECK_WRITE_W(target_seg);                                                                   \
            dst_reg = MEM_LOAD_ADDR_EA_W_NO_ABRT(target_seg);                                                \
        }                                                                                                    \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN16);                                       \
        src_reg = LOAD_REG_W((fetchdat >> 3) & 7);                                                           \
        op##_HOST_REG_W(dst_reg, src_reg);                                                                   \
        STORE_HOST_REG_ADDR_WL((uintptr_t) &cpu_state.flags_res, dst_reg);                                   \
        if (writeback) {                                                                                     \
            if ((fetchdat & 0xc0) == 0xc0)                                                                   \
                STORE_REG_W_RELEASE(dst_reg);                                                                \
            else {                                                                                           \
                LOAD_EA();                                                                                   \
                MEM_STORE_ADDR_EA_W_NO_ABRT(target_seg, dst_reg);                                            \
            }                                                                                                \
        } else                                                                                               \
            RELEASE_REG(dst_reg);                                                                            \
        RELEASE_REG(src_reg);                                                                                \
                                                                                                             \
        return op_pc + 1;                                                                                    \
    }                                                                                                        \
    static uint32_t                                                                                          \
    rop##name##_l_rmw(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block)) \
    {                                                                                                        \
        int     src_reg;                                                                                     \
        int     dst_reg;                                                                                     \
        x86seg *target_seg;                                                                                  \
                                                                                                             \
        if ((fetchdat & 0xc0) == 0xc0) {                                                                     \
            dst_reg = LOAD_REG_L(fetchdat & 7);                                                              \
        } else {                                                                                             \
            target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                             \
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);                                       \
            SAVE_EA();                                                                                       \
            MEM_CHECK_WRITE_L(target_seg);                                                                   \
            dst_reg = MEM_LOAD_ADDR_EA_L_NO_ABRT(target_seg);                                                \
        }                                                                                                    \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN32);                                       \
        src_reg = LOAD_REG_L((fetchdat >> 3) & 7);                                                           \
        op##_HOST_REG_L(dst_reg, src_reg);                                                                   \
        STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, dst_reg);                                      \
        if (writeback) {                                                                                     \
            if ((fetchdat & 0xc0) == 0xc0)                                                                   \
                STORE_REG_L_RELEASE(dst_reg);                                                                \
            else {                                                                                           \
                LOAD_EA();                                                                                   \
                MEM_STORE_ADDR_EA_L_NO_ABRT(target_seg, dst_reg);                                            \
            }                                                                                                \
        } else                                                                                               \
            RELEASE_REG(dst_reg);                                                                            \
        RELEASE_REG(src_reg);                                                                                \
                                                                                                             \
        return op_pc + 1;                                                                                    \
    }                                                                                                        \
    static uint32_t                                                                                          \
    rop##name##_b_rm(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block)) \
    {                                                                                                        \
        int src_reg;                                                                                         \
        int dst_reg;                                                                                         \
                                                                                                             \
        if ((fetchdat & 0xc0) == 0xc0) {                                                                     \
            src_reg = LOAD_REG_B(fetchdat & 7);                                                              \
        } else {                                                                                             \
            x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                     \
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);                                       \
            MEM_LOAD_ADDR_EA_B(target_seg);                                                                  \
            src_reg = 0;                                                                                     \
        }                                                                                                    \
                                                                                                             \
        dst_reg = LOAD_REG_B((fetchdat >> 3) & 7);                                                           \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN8);                                        \
        op##_HOST_REG_B(dst_reg, src_reg);                                                                   \
        STORE_HOST_REG_ADDR_BL((uintptr_t) &cpu_state.flags_res, dst_reg);                                   \
        if (writeback)                                                                                       \
            STORE_REG_B_RELEASE(dst_reg);                                                                    \
        else                                                                                                 \
            RELEASE_REG(dst_reg);                                                                            \
        RELEASE_REG(src_reg);                                                                                \
                                                                                                             \
        return op_pc + 1;                                                                                    \
    }                                                                                                        \
    static uint32_t                                                                                          \
    rop##name##_w_rm(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block)) \
    {                                                                                                        \
        int src_reg;                                                                                         \
        int dst_reg;                                                                                         \
                                                                                                             \
        if ((fetchdat & 0xc0) == 0xc0) {                                                                     \
            src_reg = LOAD_REG_W(fetchdat & 7);                                                              \
        } else {                                                                                             \
            x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                     \
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);                                       \
            MEM_LOAD_ADDR_EA_W(target_seg);                                                                  \
            src_reg = 0;                                                                                     \
        }                                                                                                    \
                                                                                                             \
        dst_reg = LOAD_REG_W((fetchdat >> 3) & 7);                                                           \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN16);                                       \
        op##_HOST_REG_W(dst_reg, src_reg);                                                                   \
        STORE_HOST_REG_ADDR_WL((uintptr_t) &cpu_state.flags_res, dst_reg);                                   \
        if (writeback)                                                                                       \
            STORE_REG_W_RELEASE(dst_reg);                                                                    \
        else                                                                                                 \
            RELEASE_REG(dst_reg);                                                                            \
        RELEASE_REG(src_reg);                                                                                \
                                                                                                             \
        return op_pc + 1;                                                                                    \
    }                                                                                                        \
    static uint32_t                                                                                          \
    rop##name##_l_rm(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block)) \
    {                                                                                                        \
        int src_reg;                                                                                         \
        int dst_reg;                                                                                         \
                                                                                                             \
        if ((fetchdat & 0xc0) == 0xc0) {                                                                     \
            src_reg = LOAD_REG_L(fetchdat & 7);                                                              \
        } else {                                                                                             \
            x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                     \
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);                                       \
            MEM_LOAD_ADDR_EA_L(target_seg);                                                                  \
            src_reg = 0;                                                                                     \
        }                                                                                                    \
                                                                                                             \
        dst_reg = LOAD_REG_L((fetchdat >> 3) & 7);                                                           \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN32);                                       \
        op##_HOST_REG_L(dst_reg, src_reg);                                                                   \
        STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, dst_reg);                                      \
        if (writeback)                                                                                       \
            STORE_REG_L_RELEASE(dst_reg);                                                                    \
        else                                                                                                 \
            RELEASE_REG(dst_reg);                                                                            \
        RELEASE_REG(src_reg);                                                                                \
                                                                                                             \
        return op_pc + 1;                                                                                    \
    }

ROP_LOGIC(AND, AND, 1)
ROP_LOGIC(OR, OR, 1)
ROP_LOGIC(XOR, XOR, 1)

static uint32_t
ropTEST_b_rm(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int src_reg;
    int dst_reg;

    if ((fetchdat & 0xc0) == 0xc0) {
        src_reg = LOAD_REG_B(fetchdat & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);
        MEM_LOAD_ADDR_EA_B(target_seg);
        src_reg = 0;
    }

    dst_reg = LOAD_REG_B((fetchdat >> 3) & 7);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN8);
    dst_reg = TEST_HOST_REG_B(dst_reg, src_reg);
    STORE_HOST_REG_ADDR_BL((uintptr_t) &cpu_state.flags_res, dst_reg);
    RELEASE_REG(dst_reg);
    RELEASE_REG(src_reg);

    return op_pc + 1;
}
static uint32_t
ropTEST_w_rm(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int src_reg;
    int dst_reg;

    if ((fetchdat & 0xc0) == 0xc0) {
        src_reg = LOAD_REG_W(fetchdat & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);
        MEM_LOAD_ADDR_EA_W(target_seg);
        src_reg = 0;
    }

    dst_reg = LOAD_REG_W((fetchdat >> 3) & 7);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN16);
    dst_reg = TEST_HOST_REG_W(dst_reg, src_reg);
    STORE_HOST_REG_ADDR_WL((uintptr_t) &cpu_state.flags_res, dst_reg);
    RELEASE_REG(dst_reg);
    RELEASE_REG(src_reg);

    return op_pc + 1;
}
static uint32_t
ropTEST_l_rm(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int src_reg;
    int dst_reg;

    if ((fetchdat & 0xc0) == 0xc0) {
        src_reg = LOAD_REG_L(fetchdat & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);
        MEM_LOAD_ADDR_EA_L(target_seg);
        src_reg = 0;
    }

    dst_reg = LOAD_REG_L((fetchdat >> 3) & 7);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN32);
    dst_reg = TEST_HOST_REG_L(dst_reg, src_reg);
    STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, dst_reg);
    RELEASE_REG(dst_reg);
    RELEASE_REG(src_reg);

    return op_pc + 1;
}

static uint32_t
ropAND_AL_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_B(REG_AL);

    AND_HOST_REG_IMM(host_reg, (fetchdat & 0xff) | 0xffffff00);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN8);
    STORE_HOST_REG_ADDR_BL((uintptr_t) &cpu_state.flags_res, host_reg);
    STORE_REG_B_RELEASE(host_reg);

    return op_pc + 1;
}
static uint32_t
ropAND_AX_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_W(REG_AX);

    AND_HOST_REG_IMM(host_reg, (fetchdat & 0xffff) | 0xffff0000);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN16);
    STORE_HOST_REG_ADDR_WL((uintptr_t) &cpu_state.flags_res, host_reg);
    STORE_REG_W_RELEASE(host_reg);

    return op_pc + 2;
}
static uint32_t
ropAND_EAX_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_L(REG_EAX);

    fetchdat = fastreadl(cs + op_pc);
    AND_HOST_REG_IMM(host_reg, fetchdat);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN32);
    STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, host_reg);
    STORE_REG_L_RELEASE(host_reg);

    return op_pc + 4;
}

static uint32_t
ropOR_AL_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_B(REG_AL);

    OR_HOST_REG_IMM(host_reg, fetchdat & 0xff);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN8);
    STORE_HOST_REG_ADDR_BL((uintptr_t) &cpu_state.flags_res, host_reg);
    STORE_REG_B_RELEASE(host_reg);

    return op_pc + 1;
}
static uint32_t
ropOR_AX_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_W(REG_AX);

    OR_HOST_REG_IMM(host_reg, fetchdat & 0xffff);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN16);
    STORE_HOST_REG_ADDR_WL((uintptr_t) &cpu_state.flags_res, host_reg);
    STORE_REG_W_RELEASE(host_reg);

    return op_pc + 2;
}
static uint32_t
ropOR_EAX_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_L(REG_EAX);

    fetchdat = fastreadl(cs + op_pc);
    OR_HOST_REG_IMM(host_reg, fetchdat);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN32);
    STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, host_reg);
    STORE_REG_L_RELEASE(host_reg);

    return op_pc + 4;
}

static uint32_t
ropTEST_AL_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_B(REG_AL);

    host_reg = TEST_HOST_REG_IMM(host_reg, fetchdat & 0xff);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN8);
    STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, host_reg);
    RELEASE_REG(host_reg);

    return op_pc + 1;
}
static uint32_t
ropTEST_AX_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_W(REG_AX);

    host_reg = TEST_HOST_REG_IMM(host_reg, fetchdat & 0xffff);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN16);
    STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, host_reg);
    RELEASE_REG(host_reg);

    return op_pc + 2;
}
static uint32_t
ropTEST_EAX_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_L(REG_EAX);

    fetchdat = fastreadl(cs + op_pc);
    host_reg = TEST_HOST_REG_IMM(host_reg, fetchdat);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN32);
    STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, host_reg);
    RELEASE_REG(host_reg);

    return op_pc + 4;
}

static uint32_t
ropXOR_AL_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_B(REG_AL);

    XOR_HOST_REG_IMM(host_reg, fetchdat & 0xff);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN8);
    STORE_HOST_REG_ADDR_BL((uintptr_t) &cpu_state.flags_res, host_reg);
    STORE_REG_B_RELEASE(host_reg);

    return op_pc + 1;
}
static uint32_t
ropXOR_AX_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_W(REG_AX);

    XOR_HOST_REG_IMM(host_reg, fetchdat & 0xffff);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN16);
    STORE_HOST_REG_ADDR_WL((uintptr_t) &cpu_state.flags_res, host_reg);
    STORE_REG_W_RELEASE(host_reg);

    return op_pc + 2;
}
static uint32_t
ropXOR_EAX_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_L(REG_EAX);

    fetchdat = fastreadl(cs + op_pc);
    XOR_HOST_REG_IMM(host_reg, fetchdat);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN32);
    STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, host_reg);
    STORE_REG_L_RELEASE(host_reg);

    return op_pc + 4;
}

static uint32_t
ropF6(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg *target_seg;
    int     host_reg;
    uint8_t imm;

    switch (fetchdat & 0x38) {
        case 0x00: /*TEST b,#8*/
            if ((fetchdat & 0xc0) == 0xc0) {
                host_reg = LOAD_REG_B(fetchdat & 7);
                imm      = (fetchdat >> 8) & 0xff;
            } else {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                imm        = fastreadb(cs + op_pc + 1);
                STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_B(target_seg);
                host_reg = 0;
            }
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN8);
            host_reg = TEST_HOST_REG_IMM(host_reg, imm);
            STORE_HOST_REG_ADDR_BL((uintptr_t) &cpu_state.flags_res, host_reg);
            RELEASE_REG(host_reg);
            return op_pc + 2;

        case 0x10: /*NOT b*/
            if ((fetchdat & 0xc0) != 0xc0)
                return 0;
            host_reg = LOAD_REG_B(fetchdat & 7);
            XOR_HOST_REG_IMM(host_reg, 0xff);
            STORE_REG_B_RELEASE(host_reg);
            return op_pc + 1;

        case 0x18: /*NEG b*/
            if ((fetchdat & 0xc0) != 0xc0)
                return 0;
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_SUB8);
            host_reg = LOAD_REG_B(fetchdat & 7);
            STORE_HOST_REG_ADDR_BL((uintptr_t) &cpu_state.flags_op2, host_reg);
            NEG_HOST_REG_B(host_reg);
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op1, 0);
            STORE_REG_B_RELEASE(host_reg);
            STORE_HOST_REG_ADDR_BL((uintptr_t) &cpu_state.flags_res, host_reg);
            return op_pc + 1;
    }

    return 0;
}
static uint32_t
ropF7_w(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg  *target_seg;
    int      host_reg;
    uint16_t imm;

    switch (fetchdat & 0x38) {
        case 0x00: /*TEST w,#*/
            if ((fetchdat & 0xc0) == 0xc0) {
                host_reg = LOAD_REG_W(fetchdat & 7);
                imm      = (fetchdat >> 8) & 0xffff;
            } else {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                imm        = fastreadw(cs + op_pc + 1);
                STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_W(target_seg);
                host_reg = 0;
            }
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN16);
            host_reg = TEST_HOST_REG_IMM(host_reg, imm);
            STORE_HOST_REG_ADDR_WL((uintptr_t) &cpu_state.flags_res, host_reg);
            RELEASE_REG(host_reg);
            return op_pc + 3;

        case 0x10: /*NOT w*/
            if ((fetchdat & 0xc0) != 0xc0)
                return 0;
            host_reg = LOAD_REG_W(fetchdat & 7);
            XOR_HOST_REG_IMM(host_reg, 0xffff);
            STORE_REG_W_RELEASE(host_reg);
            return op_pc + 1;

        case 0x18: /*NEG w*/
            if ((fetchdat & 0xc0) != 0xc0)
                return 0;
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_SUB16);
            host_reg = LOAD_REG_W(fetchdat & 7);
            STORE_HOST_REG_ADDR_WL((uintptr_t) &cpu_state.flags_op2, host_reg);
            NEG_HOST_REG_W(host_reg);
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op1, 0);
            STORE_REG_W_RELEASE(host_reg);
            STORE_HOST_REG_ADDR_WL((uintptr_t) &cpu_state.flags_res, host_reg);
            return op_pc + 1;
    }

    return 0;
}
static uint32_t
ropF7_l(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    x86seg  *target_seg;
    int      host_reg;
    uint32_t imm;

    switch (fetchdat & 0x38) {
        case 0x00: /*TEST l,#*/
            if ((fetchdat & 0xc0) == 0xc0) {
                host_reg = LOAD_REG_L(fetchdat & 7);
                imm      = fastreadl(cs + op_pc + 1);
            } else {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                imm        = fastreadl(cs + op_pc + 1);
                STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_L(target_seg);
                host_reg = 0;
            }
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_ZN32);
            host_reg = TEST_HOST_REG_IMM(host_reg, imm);
            STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, host_reg);
            RELEASE_REG(host_reg);
            return op_pc + 5;

        case 0x10: /*NOT l*/
            if ((fetchdat & 0xc0) != 0xc0)
                return 0;
            host_reg = LOAD_REG_L(fetchdat & 7);
            XOR_HOST_REG_IMM(host_reg, 0xffffffff);
            STORE_REG_L_RELEASE(host_reg);
            return op_pc + 1;

        case 0x18: /*NEG l*/
            if ((fetchdat & 0xc0) != 0xc0)
                return 0;
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op, FLAGS_SUB32);
            host_reg = LOAD_REG_L(fetchdat & 7);
            STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_op2, host_reg);
            NEG_HOST_REG_L(host_reg);
            STORE_IMM_ADDR_L((uintptr_t) &cpu_state.flags_op1, 0);
            STORE_REG_L_RELEASE(host_reg);
            STORE_HOST_REG_ADDR((uintptr_t) &cpu_state.flags_res, host_reg);
            return op_pc + 1;
    }

    return 0;
}
