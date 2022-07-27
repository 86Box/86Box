#define OP_XCHG_AX_(reg)                                                                                                                \
        static uint32_t ropXCHG_AX_ ## reg(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)       \
        {                                                                                                                               \
                int ax_reg, host_reg, temp_reg;                                                                                         \
                                                                                                                                        \
                ax_reg = LOAD_REG_W(REG_AX);                                                                                            \
                host_reg = LOAD_REG_W(REG_ ## reg);                                                                                     \
                temp_reg = COPY_REG(host_reg);                                                                                          \
                STORE_REG_TARGET_W_RELEASE(ax_reg, REG_ ## reg);                                                                        \
                STORE_REG_TARGET_W_RELEASE(temp_reg, REG_AX);                                                                           \
                                                                                                                                        \
                return op_pc;                                                                                                           \
        }

OP_XCHG_AX_(BX)
OP_XCHG_AX_(CX)
OP_XCHG_AX_(DX)
OP_XCHG_AX_(SI)
OP_XCHG_AX_(DI)
OP_XCHG_AX_(SP)
OP_XCHG_AX_(BP)

#define OP_XCHG_EAX_(reg)                                                                                                               \
        static uint32_t ropXCHG_EAX_ ## reg(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)      \
        {                                                                                                                               \
                int eax_reg, host_reg, temp_reg;                                                                                        \
                                                                                                                                        \
                eax_reg = LOAD_REG_L(REG_EAX);                                                                                          \
                host_reg = LOAD_REG_L(REG_ ## reg);                                                                                     \
                temp_reg = COPY_REG(host_reg);                                                                                          \
                STORE_REG_TARGET_L_RELEASE(eax_reg, REG_ ## reg);                                                                       \
                STORE_REG_TARGET_L_RELEASE(temp_reg, REG_EAX);                                                                          \
                                                                                                                                        \
                return op_pc;                                                                                                           \
        }

OP_XCHG_EAX_(EBX)
OP_XCHG_EAX_(ECX)
OP_XCHG_EAX_(EDX)
OP_XCHG_EAX_(ESI)
OP_XCHG_EAX_(EDI)
OP_XCHG_EAX_(ESP)
OP_XCHG_EAX_(EBP)

static uint32_t ropXCHG_b(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int src_reg, dst_reg, temp_reg;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;

        dst_reg = LOAD_REG_B(fetchdat & 7);
        src_reg = LOAD_REG_B((fetchdat >> 3) & 7);
        temp_reg = COPY_REG(src_reg);
        STORE_REG_TARGET_B_RELEASE(dst_reg, (fetchdat >> 3) & 7);
        STORE_REG_TARGET_B_RELEASE(temp_reg, fetchdat & 7);

        return op_pc + 1;
}
static uint32_t ropXCHG_w(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int src_reg, dst_reg, temp_reg;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;

        dst_reg = LOAD_REG_W(fetchdat & 7);
        src_reg = LOAD_REG_W((fetchdat >> 3) & 7);
        temp_reg = COPY_REG(src_reg);
        STORE_REG_TARGET_W_RELEASE(dst_reg, (fetchdat >> 3) & 7);
        STORE_REG_TARGET_W_RELEASE(temp_reg, fetchdat & 7);

        return op_pc + 1;
}
static uint32_t ropXCHG_l(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int src_reg, dst_reg, temp_reg;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;

        dst_reg = LOAD_REG_L(fetchdat & 7);
        src_reg = LOAD_REG_L((fetchdat >> 3) & 7);
        temp_reg = COPY_REG(src_reg);
        STORE_REG_TARGET_L_RELEASE(dst_reg, (fetchdat >> 3) & 7);
        STORE_REG_TARGET_L_RELEASE(temp_reg, fetchdat & 7);

        return op_pc + 1;
}
