static uint32_t
ropMOV_rb_imm(uint8_t opcode, uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    STORE_IMM_REG_B(opcode & 7, fetchdat & 0xff);

    return op_pc + 1;
}
static uint32_t
ropMOV_rw_imm(uint8_t opcode, uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    STORE_IMM_REG_W(opcode & 7, fetchdat & 0xffff);

    return op_pc + 2;
}
static uint32_t
ropMOV_rl_imm(uint8_t opcode, uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc, UNUSED(codeblock_t *block))
{
    fetchdat = fastreadl(cs + op_pc);

    STORE_IMM_REG_L(opcode & 7, fetchdat);

    return op_pc + 4;
}

static uint32_t
ropMOV_b_r(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_B((fetchdat >> 3) & 7);

    if ((fetchdat & 0xc0) == 0xc0) {
        STORE_REG_TARGET_B_RELEASE(host_reg, fetchdat & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_WRITE(target_seg);
        CHECK_SEG_LIMITS(target_seg, 0);

        MEM_STORE_ADDR_EA_B(target_seg, host_reg);
        RELEASE_REG(host_reg);
    }

    return op_pc + 1;
}
static uint32_t
ropMOV_w_r(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg = LOAD_REG_W((fetchdat >> 3) & 7);

    if ((fetchdat & 0xc0) == 0xc0) {
        STORE_REG_TARGET_W_RELEASE(host_reg, fetchdat & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_WRITE(target_seg);
        CHECK_SEG_LIMITS(target_seg, 1);

        MEM_STORE_ADDR_EA_W(target_seg, host_reg);
        RELEASE_REG(host_reg);
    }

    return op_pc + 1;
}

static uint32_t
ropMOV_l_r(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg;

    host_reg = LOAD_REG_L((fetchdat >> 3) & 7);

    if ((fetchdat & 0xc0) == 0xc0) {
        STORE_REG_TARGET_L_RELEASE(host_reg, fetchdat & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_WRITE(target_seg);
        CHECK_SEG_LIMITS(target_seg, 3);

        MEM_STORE_ADDR_EA_L(target_seg, host_reg);
        RELEASE_REG(host_reg);
    }

    return op_pc + 1;
}

static uint32_t
ropMOV_r_b(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int host_reg = LOAD_REG_B(fetchdat & 7);
        STORE_REG_TARGET_B_RELEASE(host_reg, (fetchdat >> 3) & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_B(target_seg);
        STORE_REG_TARGET_B_RELEASE(0, (fetchdat >> 3) & 7);
    }

    return op_pc + 1;
}
static uint32_t
ropMOV_r_w(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int host_reg = LOAD_REG_W(fetchdat & 7);
        STORE_REG_TARGET_W_RELEASE(host_reg, (fetchdat >> 3) & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_W(target_seg);
        STORE_REG_TARGET_W_RELEASE(0, (fetchdat >> 3) & 7);
    }

    return op_pc + 1;
}
static uint32_t
ropMOV_r_l(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int host_reg = LOAD_REG_L(fetchdat & 7);
        STORE_REG_TARGET_L_RELEASE(host_reg, (fetchdat >> 3) & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_L(target_seg);
        STORE_REG_TARGET_L_RELEASE(0, (fetchdat >> 3) & 7);
    }

    return op_pc + 1;
}

static uint32_t
ropMOV_b_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        STORE_IMM_REG_B(fetchdat & 7, (fetchdat >> 8) & 0xff);
    } else {
        x86seg  *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
        uint32_t imm        = fastreadb(cs + op_pc + 1);
        int      host_reg   = LOAD_REG_IMM(imm);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);
        CHECK_SEG_WRITE(target_seg);

        MEM_STORE_ADDR_EA_B(target_seg, host_reg);
        RELEASE_REG(host_reg);
    }

    return op_pc + 2;
}
static uint32_t
ropMOV_w_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        STORE_IMM_REG_W(fetchdat & 7, (fetchdat >> 8) & 0xffff);
    } else {
        x86seg  *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
        uint32_t imm        = fastreadw(cs + op_pc + 1);
        int      host_reg   = LOAD_REG_IMM(imm);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);
        CHECK_SEG_WRITE(target_seg);

        MEM_STORE_ADDR_EA_W(target_seg, host_reg);
        RELEASE_REG(host_reg);
    }

    return op_pc + 3;
}
static uint32_t
ropMOV_l_imm(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        uint32_t imm = fastreadl(cs + op_pc + 1);

        STORE_IMM_REG_L(fetchdat & 7, imm);
    } else {
        x86seg  *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
        uint32_t imm        = fastreadl(cs + op_pc + 1);
        int      host_reg   = LOAD_REG_IMM(imm);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);
        CHECK_SEG_WRITE(target_seg);

        MEM_STORE_ADDR_EA_L(target_seg, host_reg);
        RELEASE_REG(host_reg);
    }

    return op_pc + 5;
}

static uint32_t
ropMOV_AL_a(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t addr;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    CHECK_SEG_READ(op_ea_seg);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    MEM_LOAD_ADDR_IMM_B(op_ea_seg, addr);
    STORE_REG_TARGET_B_RELEASE(0, REG_AL);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
static uint32_t
ropMOV_AX_a(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t addr;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    CHECK_SEG_READ(op_ea_seg);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    MEM_LOAD_ADDR_IMM_W(op_ea_seg, addr);
    STORE_REG_TARGET_W_RELEASE(0, REG_AX);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
static uint32_t
ropMOV_EAX_a(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t addr;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    CHECK_SEG_READ(op_ea_seg);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    MEM_LOAD_ADDR_IMM_L(op_ea_seg, addr);
    STORE_REG_TARGET_L_RELEASE(0, REG_EAX);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}

static uint32_t
ropMOV_a_AL(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t addr;
    int      host_reg;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    CHECK_SEG_WRITE(op_ea_seg);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    host_reg = LOAD_REG_B(REG_AL);

    MEM_STORE_ADDR_IMM_B(op_ea_seg, addr, host_reg);
    RELEASE_REG(host_reg);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
static uint32_t
ropMOV_a_AX(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t addr;
    int      host_reg;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    CHECK_SEG_WRITE(op_ea_seg);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    host_reg = LOAD_REG_W(REG_AX);

    MEM_STORE_ADDR_IMM_W(op_ea_seg, addr, host_reg);
    RELEASE_REG(host_reg);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
static uint32_t
ropMOV_a_EAX(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    uint32_t addr;
    int      host_reg;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    CHECK_SEG_WRITE(op_ea_seg);
    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    host_reg = LOAD_REG_L(REG_EAX);

    MEM_STORE_ADDR_IMM_L(op_ea_seg, addr, host_reg);
    RELEASE_REG(host_reg);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}

static uint32_t
ropLEA_w(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int dest_reg = (fetchdat >> 3) & 7;

    if ((fetchdat & 0xc0) == 0xc0)
        return 0;

    FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_REG_TARGET_W_RELEASE(0, dest_reg);

    return op_pc + 1;
}
static uint32_t
ropLEA_l(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int dest_reg = (fetchdat >> 3) & 7;

    if ((fetchdat & 0xc0) == 0xc0)
        return 0;

    FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

    STORE_REG_TARGET_L_RELEASE(0, dest_reg);

    return op_pc + 1;
}

static uint32_t
ropMOVZX_w_b(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int host_reg = LOAD_REG_B(fetchdat & 7);
        host_reg     = ZERO_EXTEND_W_B(host_reg);
        STORE_REG_TARGET_W_RELEASE(host_reg, (fetchdat >> 3) & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_B(target_seg);
        ZERO_EXTEND_W_B(0);
        STORE_REG_TARGET_W_RELEASE(0, (fetchdat >> 3) & 7);
    }

    return op_pc + 1;
}
static uint32_t
ropMOVZX_l_b(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int host_reg = LOAD_REG_B(fetchdat & 7);
        host_reg     = ZERO_EXTEND_L_B(host_reg);
        STORE_REG_TARGET_L_RELEASE(host_reg, (fetchdat >> 3) & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_B(target_seg);
        ZERO_EXTEND_L_B(0);
        STORE_REG_TARGET_L_RELEASE(0, (fetchdat >> 3) & 7);
    }

    return op_pc + 1;
}
static uint32_t
ropMOVZX_l_w(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int host_reg = LOAD_REG_W(fetchdat & 7);
        host_reg     = ZERO_EXTEND_L_W(host_reg);
        STORE_REG_TARGET_L_RELEASE(host_reg, (fetchdat >> 3) & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_W(target_seg);
        ZERO_EXTEND_L_W(0);
        STORE_REG_TARGET_L_RELEASE(0, (fetchdat >> 3) & 7);
    }

    return op_pc + 1;
}

static uint32_t
ropMOVSX_w_b(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int host_reg = LOAD_REG_B(fetchdat & 7);
        host_reg     = SIGN_EXTEND_W_B(host_reg);
        STORE_REG_TARGET_W_RELEASE(host_reg, (fetchdat >> 3) & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_B(target_seg);
        SIGN_EXTEND_W_B(0);
        STORE_REG_TARGET_W_RELEASE(0, (fetchdat >> 3) & 7);
    }

    return op_pc + 1;
}
static uint32_t
ropMOVSX_l_b(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int host_reg = LOAD_REG_B(fetchdat & 7);
        host_reg     = SIGN_EXTEND_L_B(host_reg);
        STORE_REG_TARGET_L_RELEASE(host_reg, (fetchdat >> 3) & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_B(target_seg);
        SIGN_EXTEND_L_B(0);
        STORE_REG_TARGET_L_RELEASE(0, (fetchdat >> 3) & 7);
    }

    return op_pc + 1;
}
static uint32_t
ropMOVSX_l_w(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int host_reg = LOAD_REG_W(fetchdat & 7);
        host_reg     = SIGN_EXTEND_L_W(host_reg);
        STORE_REG_TARGET_L_RELEASE(host_reg, (fetchdat >> 3) & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_READ(target_seg);

        MEM_LOAD_ADDR_EA_W(target_seg);
        SIGN_EXTEND_L_W(0);
        STORE_REG_TARGET_L_RELEASE(0, (fetchdat >> 3) & 7);
    }

    return op_pc + 1;
}

static uint32_t
ropMOV_w_seg(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg;

    switch (fetchdat & 0x38) {
        case 0x00: /*ES*/
            host_reg = LOAD_VAR_WL((uintptr_t) &ES);
            break;
        case 0x08: /*CS*/
            host_reg = LOAD_VAR_WL((uintptr_t) &CS);
            break;
        case 0x18: /*DS*/
            host_reg = LOAD_VAR_WL((uintptr_t) &DS);
            break;
        case 0x10: /*SS*/
            host_reg = LOAD_VAR_WL((uintptr_t) &SS);
            break;
        case 0x20: /*FS*/
            host_reg = LOAD_VAR_WL((uintptr_t) &FS);
            break;
        case 0x28: /*GS*/
            host_reg = LOAD_VAR_WL((uintptr_t) &GS);
            break;
        default:
            return 0;
    }

    if ((fetchdat & 0xc0) == 0xc0) {
        if (op_32 & 0x100)
            STORE_REG_TARGET_L_RELEASE(host_reg, fetchdat & 7);
        else
            STORE_REG_TARGET_W_RELEASE(host_reg, fetchdat & 7);
    } else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

        CHECK_SEG_WRITE(target_seg);
        CHECK_SEG_LIMITS(target_seg, 1);

        MEM_STORE_ADDR_EA_W(target_seg, host_reg);
        RELEASE_REG(host_reg);
    }

    return op_pc + 1;
}
static uint32_t
ropMOV_seg_w(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block))
{
    int host_reg;

    switch (fetchdat & 0x38) {
        case 0x00: /*ES*/
        case 0x18: /*DS*/
        case 0x20: /*FS*/
        case 0x28: /*GS*/
            break;
        case 0x10: /*SS*/
        default:
            return 0;
    }

    STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);

    if ((fetchdat & 0xc0) == 0xc0)
        host_reg = LOAD_REG_W(fetchdat & 7);
    else {
        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

        CHECK_SEG_READ(target_seg);
        MEM_LOAD_ADDR_EA_W(target_seg);

        host_reg = 0;
    }

    switch (fetchdat & 0x38) {
        case 0x00: /*ES*/
            LOAD_SEG(host_reg, &cpu_state.seg_es);
            break;
        case 0x18: /*DS*/
            LOAD_SEG(host_reg, &cpu_state.seg_ds);
            break;
        case 0x20: /*FS*/
            LOAD_SEG(host_reg, &cpu_state.seg_fs);
            break;
        case 0x28: /*GS*/
            LOAD_SEG(host_reg, &cpu_state.seg_gs);
            break;
    }

    return op_pc + 1;
}

#define ropLseg(seg, rseg)                                                                                           \
    static uint32_t ropL##seg(UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, UNUSED(codeblock_t *block)) \
    {                                                                                                                \
        int     dest_reg = (fetchdat >> 3) & 7;                                                                      \
        x86seg *target_seg;                                                                                          \
                                                                                                                     \
        if ((fetchdat & 0xc0) == 0xc0)                                                                               \
            return 0;                                                                                                \
                                                                                                                     \
        STORE_IMM_ADDR_L((uintptr_t) &cpu_state.oldpc, op_old_pc);                                                   \
        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                                         \
        SAVE_EA();                                                                                                   \
                                                                                                                     \
        if (op_32 & 0x100) {                                                                                         \
            MEM_LOAD_ADDR_EA_L(target_seg);                                                                          \
            STORE_HOST_REG_ADDR((uintptr_t) &codegen_temp, 0);                                                       \
            LOAD_EA();                                                                                               \
            MEM_LOAD_ADDR_EA_W_OFFSET(target_seg, 4);                                                                \
        } else {                                                                                                     \
            MEM_LOAD_ADDR_EA_W(target_seg);                                                                          \
            STORE_HOST_REG_ADDR_W((uintptr_t) &codegen_temp, 0);                                                     \
            LOAD_EA();                                                                                               \
            MEM_LOAD_ADDR_EA_W_OFFSET(target_seg, 2);                                                                \
        }                                                                                                            \
        LOAD_SEG(0, &rseg);                                                                                          \
        if (op_32 & 0x100) {                                                                                         \
                                                                                                                     \
            int host_reg = LOAD_VAR_L((uintptr_t) &codegen_temp);                                                    \
            STORE_REG_TARGET_L_RELEASE(host_reg, dest_reg);                                                          \
        } else {                                                                                                     \
            int host_reg = LOAD_VAR_W((uintptr_t) &codegen_temp);                                                    \
            STORE_REG_TARGET_W_RELEASE(host_reg, dest_reg);                                                          \
        }                                                                                                            \
                                                                                                                     \
        if (&rseg == &cpu_state.seg_ss)                                                                              \
            CPU_BLOCK_END(); /*Instruction might change stack size, so end block here*/                              \
        return op_pc + 1;                                                                                            \
    }

// clang-format off
ropLseg(DS, cpu_state.seg_ds)
ropLseg(ES, cpu_state.seg_es)
ropLseg(FS, cpu_state.seg_fs)
ropLseg(GS, cpu_state.seg_gs)
ropLseg(SS, cpu_state.seg_ss)
// clang-format on
