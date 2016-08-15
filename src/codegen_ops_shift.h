/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#define SHIFT(size, size2, count, res_store)                                    \
        STORE_IMM_ADDR_L((uint32_t)&cpu_state.flags_op2, count);                          \
        reg = LOAD_REG_ ## size(fetchdat & 7);                                  \
        res_store((uint32_t)&cpu_state.flags_op1, reg);                                   \
                                                                                \
        switch (fetchdat & 0x38)                                                \
        {                                                                       \
                case 0x20: case 0x30: /*SHL*/                                   \
                SHL_ ## size ## _IMM(reg, count);                               \
                STORE_IMM_ADDR_L((uint32_t)&cpu_state.flags_op, FLAGS_SHL ## size2);      \
                break;                                                          \
                                                                                \
                case 0x28: /*SHR*/                                              \
                SHR_ ## size ## _IMM(reg, count);                               \
                STORE_IMM_ADDR_L((uint32_t)&cpu_state.flags_op, FLAGS_SHR ## size2);      \
                break;                                                          \
                                                                                \
                case 0x38: /*SAR*/                                              \
                SAR_ ## size ## _IMM(reg, count);                               \
                STORE_IMM_ADDR_L((uint32_t)&cpu_state.flags_op, FLAGS_SAR ## size2);      \
                break;                                                          \
        }                                                                       \
                                                                                \
        res_store((uint32_t)&cpu_state.flags_res, reg);                                   \
        STORE_REG_ ## size ## _RELEASE(reg);

static uint32_t ropC0(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int count;
        int reg;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;
        if ((fetchdat & 0x38) < 0x20)
                return 0;

        count = (fetchdat >> 8) & 0x1f;
        
        SHIFT(B, 8, count, STORE_HOST_REG_ADDR_BL);

        return op_pc + 2;
}
static uint32_t ropC1_w(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int count;
        int reg;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;
        if ((fetchdat & 0x38) < 0x20)
                return 0;

        count = (fetchdat >> 8) & 0x1f;

        SHIFT(W, 16, count, STORE_HOST_REG_ADDR_WL);
        
        return op_pc + 2;
}
static uint32_t ropC1_l(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int count;
        int reg;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;
        if ((fetchdat & 0x38) < 0x20)
                return 0;

        count = (fetchdat >> 8) & 0x1f;

        SHIFT(L, 32, count, STORE_HOST_REG_ADDR);
        
        return op_pc + 2;
}

static uint32_t ropD0(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int reg;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;
        if ((fetchdat & 0x38) < 0x20)
                return 0;

        SHIFT(B, 8, 1, STORE_HOST_REG_ADDR_BL);

        return op_pc + 1;
}
static uint32_t ropD1_w(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int reg;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;
        if ((fetchdat & 0x38) < 0x20)
                return 0;

        SHIFT(W, 16, 1, STORE_HOST_REG_ADDR_WL);
        
        return op_pc + 1;
}
static uint32_t ropD1_l(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int reg;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;
        if ((fetchdat & 0x38) < 0x20)
                return 0;

        SHIFT(L, 32, 1, STORE_HOST_REG_ADDR);
        
        return op_pc + 1;
}
