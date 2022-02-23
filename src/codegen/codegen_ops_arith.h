static uint32_t ropINC_rw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;

        CALL_FUNC((uintptr_t)flags_rebuild_c);

        host_reg = LOAD_REG_W(opcode & 7);

        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
        // ADD_HOST_REG_IMM_W(host_reg, 1);
	INC_HOST_REG_W(host_reg);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_INC16);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_W_RELEASE(host_reg);

        codegen_flags_changed = 1;

        return op_pc;
}
static uint32_t ropINC_rl(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;

        CALL_FUNC((uintptr_t)flags_rebuild_c);

        host_reg = LOAD_REG_L(opcode & 7);

        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
        // ADD_HOST_REG_IMM(host_reg, 1);
	INC_HOST_REG(host_reg);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_INC32);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_L_RELEASE(host_reg);

        codegen_flags_changed = 1;

        return op_pc;
}
static uint32_t ropDEC_rw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;

        CALL_FUNC((uintptr_t)flags_rebuild_c);

        host_reg = LOAD_REG_W(opcode & 7);

        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
        // SUB_HOST_REG_IMM_W(host_reg, 1);
        DEC_HOST_REG_W(host_reg);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_DEC16);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_W_RELEASE(host_reg);

        codegen_flags_changed = 1;

        return op_pc;
}
static uint32_t ropDEC_rl(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;

        CALL_FUNC((uintptr_t)flags_rebuild_c);

        host_reg = LOAD_REG_L(opcode & 7);

        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
        // SUB_HOST_REG_IMM(host_reg, 1);
        DEC_HOST_REG(host_reg);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_DEC32);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_L_RELEASE(host_reg);

        codegen_flags_changed = 1;

        return op_pc;
}

#define ROP_ARITH_RMW(name, op, writeback) \
        static uint32_t rop ## name ## _b_rmw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)    \
        {                                                                                                                               \
                int src_reg, dst_reg;                                                                                                   \
                x86seg *target_seg;                                                                                                     \
                                                                                                                                        \
                if ((fetchdat & 0xc0) == 0xc0)                                                                                          \
                {                                                                                                                       \
                        dst_reg = LOAD_REG_B(fetchdat & 7);                                                                             \
                }                                                                                                                       \
                else                                                                                                                    \
                {                                                                                                                       \
                        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                                            \
                        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);                                                       \
                        SAVE_EA();                                                                                                      \
                        MEM_CHECK_WRITE(target_seg);                                                                                              \
                        dst_reg = MEM_LOAD_ADDR_EA_B_NO_ABRT(target_seg);                                                               \
                }                                                                                                                       \
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ ## op ## 8);                                                               \
                src_reg = LOAD_REG_B((fetchdat >> 3) & 7);                                                                              \
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, dst_reg);                                                                     \
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op2, src_reg);                                                                     \
                op ## _HOST_REG_B(dst_reg, src_reg);                                                                                    \
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, dst_reg);                                                                     \
                if (writeback)                                                                                                          \
                {                                                                                                                       \
                        if ((fetchdat & 0xc0) == 0xc0)                                                                                          \
                                STORE_REG_B_RELEASE(dst_reg);                                                                            \
                        else                                                                                                            \
                        {                                                                                                               \
                                LOAD_EA();                                                                                              \
                                MEM_STORE_ADDR_EA_B_NO_ABRT(target_seg, dst_reg);                                                       \
                        }                                                                                                               \
                }                                                                                                                       \
                else                                                                                                                    \
                        RELEASE_REG(dst_reg);                                                                                    \
                RELEASE_REG(src_reg);                                                                                                   \
                                                                                                                                        \
                codegen_flags_changed = 1;                                                                                              \
                return op_pc + 1;                                                                                                       \
        }                                                                                                                               \
        static uint32_t rop ## name ## _w_rmw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)    \
        {                                                                                                                               \
                int src_reg, dst_reg;                                                                                                   \
                x86seg *target_seg;                                                                                                     \
                                                                                                                                        \
                if ((fetchdat & 0xc0) == 0xc0)                                                                                          \
                {                                                                                                                       \
                        dst_reg = LOAD_REG_W(fetchdat & 7);                                                                             \
                }                                                                                                                       \
                else                                                                                                                    \
                {                                                                                                                       \
                        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                                            \
                        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);                                                       \
                        SAVE_EA();                                                                                                      \
                        MEM_CHECK_WRITE_W(target_seg);                                                                                              \
                        dst_reg = MEM_LOAD_ADDR_EA_W_NO_ABRT(target_seg);                                                               \
                }                                                                                                                       \
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ ## op ## 16);                                                              \
                src_reg = LOAD_REG_W((fetchdat >> 3) & 7);                                                                              \
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, dst_reg);                                                                     \
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op2, src_reg);                                                                     \
                op ## _HOST_REG_W(dst_reg, src_reg);                                                                                    \
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, dst_reg);                                                                     \
                if (writeback)                                                                                                          \
                {                                                                                                                       \
                        if ((fetchdat & 0xc0) == 0xc0)                                                                                          \
                                STORE_REG_W_RELEASE(dst_reg);                                                                            \
                        else                                                                                                            \
                        {                                                                                                               \
                                LOAD_EA();                                                                                              \
                                MEM_STORE_ADDR_EA_W_NO_ABRT(target_seg, dst_reg);                                                       \
                        }                                                                                                               \
                }                                                                                                                       \
                else                                                                                                                    \
                        RELEASE_REG(dst_reg);                                                                                    \
                RELEASE_REG(src_reg);                                                                                                   \
                                                                                                                                        \
                codegen_flags_changed = 1;                                                                                              \
                return op_pc + 1;                                                                                                       \
        }                                                                                                                               \
        static uint32_t rop ## name ## _l_rmw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)    \
        {                                                                                                                               \
                int src_reg, dst_reg;                                                                                                   \
                x86seg *target_seg;                                                                                                     \
                                                                                                                                        \
                if ((fetchdat & 0xc0) == 0xc0)                                                                                          \
                {                                                                                                                       \
                        dst_reg = LOAD_REG_L(fetchdat & 7);                                                                             \
                }                                                                                                                       \
                else                                                                                                                    \
                {                                                                                                                       \
                        target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                                            \
                        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);                                                       \
                        SAVE_EA();                                                                                                      \
                        MEM_CHECK_WRITE_L(target_seg);                                                                                              \
                        dst_reg = MEM_LOAD_ADDR_EA_L_NO_ABRT(target_seg);                                                               \
                }                                                                                                                       \
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ ## op ## 32);                                                              \
                src_reg = LOAD_REG_L((fetchdat >> 3) & 7);                                                                              \
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, dst_reg);                                                                     \
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op2, src_reg);                                                                     \
                op ## _HOST_REG_L(dst_reg, src_reg);                                                                                    \
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, dst_reg);                                                                     \
                if (writeback)                                                                                                          \
                {                                                                                                                       \
                        if ((fetchdat & 0xc0) == 0xc0)                                                                                          \
                                STORE_REG_L_RELEASE(dst_reg);                                                                            \
                        else                                                                                                            \
                        {                                                                                                               \
                                LOAD_EA();                                                                                              \
                                MEM_STORE_ADDR_EA_L_NO_ABRT(target_seg, dst_reg);                                                       \
                        }                                                                                                               \
                }                                                                                                                       \
                else                                                                                                                    \
                        RELEASE_REG(dst_reg);                                                                                    \
                RELEASE_REG(src_reg);                                                                                                   \
                                                                                                                                        \
                codegen_flags_changed = 1;                                                                                              \
                return op_pc + 1;                                                                                                       \
        }

#define ROP_ARITH_RM(name, op, writeback) \
        static uint32_t rop ## name ## _b_rm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)     \
        {                                                                                                                               \
                int src_reg, dst_reg;                                                                                                   \
                                                                                                                                        \
                if ((fetchdat & 0xc0) == 0xc0)                                                                                          \
                {                                                                                                                       \
                        src_reg = LOAD_REG_B(fetchdat & 7);                                                                             \
                }                                                                                                                       \
                else                                                                                                                    \
                {                                                                                                                       \
                        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                                    \
                        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);                                                                 \
                        MEM_LOAD_ADDR_EA_B(target_seg);                                                                                 \
                        src_reg = 0;                                                                                                    \
                }                                                                                                                       \
                                                                                                                                        \
                dst_reg = LOAD_REG_B((fetchdat >> 3) & 7);                                                                              \
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ ## op ## 8);                                                               \
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, dst_reg);                                                                     \
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op2, src_reg);                                                                     \
                op ## _HOST_REG_B(dst_reg, src_reg);                                                                                    \
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, dst_reg);                                                                     \
                if (writeback) STORE_REG_B_RELEASE(dst_reg);                                                                            \
                else           RELEASE_REG(dst_reg);                                                                                    \
                RELEASE_REG(src_reg);                                                                                                   \
                                                                                                                                        \
                codegen_flags_changed = 1;                                                                                              \
                return op_pc + 1;                                                                                                       \
        }                                                                                                                               \
        static uint32_t rop ## name ## _w_rm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)     \
        {                                                                                                                               \
                int src_reg, dst_reg;                                                                                                   \
                                                                                                                                        \
                if ((fetchdat & 0xc0) == 0xc0)                                                                                          \
                {                                                                                                                       \
                        src_reg = LOAD_REG_W(fetchdat & 7);                                                                             \
                }                                                                                                                       \
                else                                                                                                                    \
                {                                                                                                                       \
                        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                                    \
                        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);                                                                 \
                        MEM_LOAD_ADDR_EA_W(target_seg);                                                                                 \
                        src_reg = 0;                                                                                                    \
                }                                                                                                                       \
                                                                                                                                        \
                dst_reg = LOAD_REG_W((fetchdat >> 3) & 7);                                                                              \
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ ## op ## 16);                                                              \
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, dst_reg);                                                                     \
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op2, src_reg);                                                                     \
                op ## _HOST_REG_W(dst_reg, src_reg);                                                                                    \
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, dst_reg);                                                                     \
                if (writeback) STORE_REG_W_RELEASE(dst_reg);                                                                            \
                else           RELEASE_REG(dst_reg);                                                                                    \
                RELEASE_REG(src_reg);                                                                                                   \
                                                                                                                                        \
                codegen_flags_changed = 1;                                                                                              \
                return op_pc + 1;                                                                                                       \
        }                                                                                                                               \
        static uint32_t rop ## name ## _l_rm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)     \
        {                                                                                                                               \
                int src_reg, dst_reg;                                                                                                   \
                                                                                                                                        \
                if ((fetchdat & 0xc0) == 0xc0)                                                                                          \
                {                                                                                                                       \
                        src_reg = LOAD_REG_L(fetchdat & 7);                                                                             \
                }                                                                                                                       \
                else                                                                                                                    \
                {                                                                                                                       \
                        x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                                    \
                        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);                                                                 \
                        MEM_LOAD_ADDR_EA_L(target_seg);                                                                                 \
                        src_reg = 0;                                                                                                    \
                }                                                                                                                       \
                                                                                                                                        \
                dst_reg = LOAD_REG_L((fetchdat >> 3) & 7);                                                                              \
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ ## op ## 32);                                                              \
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, dst_reg);                                                                     \
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op2, src_reg);                                                                     \
                op ## _HOST_REG_L(dst_reg, src_reg);                                                                                    \
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, dst_reg);                                                                     \
                if (writeback) STORE_REG_L_RELEASE(dst_reg);                                                                            \
                else           RELEASE_REG(dst_reg);                                                                                    \
                RELEASE_REG(src_reg);                                                                                                   \
                                                                                                                                        \
                codegen_flags_changed = 1;                                                                                              \
                return op_pc + 1;                                                                                                       \
        }

ROP_ARITH_RMW(ADD, ADD, 1)
ROP_ARITH_RMW(SUB, SUB, 1)
ROP_ARITH_RM(ADD, ADD, 1)
ROP_ARITH_RM(SUB, SUB, 1)

static uint32_t ropCMP_b_rm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int src_reg, dst_reg;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                src_reg = LOAD_REG_B(fetchdat & 7);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_B(target_seg);
                src_reg = 0;
        }

        dst_reg = LOAD_REG_B((fetchdat >> 3) & 7);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB8);
        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, dst_reg);
        dst_reg = CMP_HOST_REG_B(dst_reg, src_reg);
        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op2, src_reg);
        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, dst_reg);
        RELEASE_REG(dst_reg);
        RELEASE_REG(src_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
static uint32_t ropCMP_w_rm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int src_reg, dst_reg;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                src_reg = LOAD_REG_W(fetchdat & 7);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_W(target_seg);
                src_reg = 0;
        }

        dst_reg = LOAD_REG_W((fetchdat >> 3) & 7);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB16);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, dst_reg);
        dst_reg = CMP_HOST_REG_W(dst_reg, src_reg);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op2, src_reg);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, dst_reg);
        RELEASE_REG(dst_reg);
        RELEASE_REG(src_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
static uint32_t ropCMP_l_rm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int src_reg, dst_reg;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                src_reg = LOAD_REG_L(fetchdat & 7);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_L(target_seg);
                src_reg = 0;
        }

        dst_reg = LOAD_REG_L((fetchdat >> 3) & 7);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB32);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, dst_reg);
        dst_reg = CMP_HOST_REG_L(dst_reg, src_reg);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op2, src_reg);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, dst_reg);
        RELEASE_REG(dst_reg);
        RELEASE_REG(src_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}

static uint32_t ropCMP_b_rmw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int src_reg, dst_reg;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                dst_reg = LOAD_REG_B(fetchdat & 7);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_B(target_seg);
                dst_reg = 0;
        }

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB8);
        src_reg = LOAD_REG_B((fetchdat >> 3) & 7);
        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, dst_reg);
        dst_reg = CMP_HOST_REG_B(dst_reg, src_reg);
        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op2, src_reg);
        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, dst_reg);
        RELEASE_REG(dst_reg);
        RELEASE_REG(src_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
static uint32_t ropCMP_w_rmw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int src_reg, dst_reg;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                dst_reg = LOAD_REG_W(fetchdat & 7);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_W(target_seg);
                dst_reg = 0;
        }                                                                                                                       \

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB16);
        src_reg = LOAD_REG_W((fetchdat >> 3) & 7);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, dst_reg);
        dst_reg = CMP_HOST_REG_W(dst_reg, src_reg);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op2, src_reg);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, dst_reg);
        RELEASE_REG(dst_reg);
        RELEASE_REG(src_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
static uint32_t ropCMP_l_rmw(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int src_reg, dst_reg;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                dst_reg = LOAD_REG_L(fetchdat & 7);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_L(target_seg);
                dst_reg = 0;
        }

        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB32);
        src_reg = LOAD_REG_L((fetchdat >> 3) & 7);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, dst_reg);
        dst_reg = CMP_HOST_REG_L(dst_reg, src_reg);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op2, src_reg);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, dst_reg);
        RELEASE_REG(dst_reg);
        RELEASE_REG(src_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}


static uint32_t ropADD_AL_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg = LOAD_REG_B(REG_AL);

        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, host_reg);
        ADD_HOST_REG_IMM_B(host_reg, fetchdat & 0xff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, fetchdat & 0xff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ADD8);
        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_B_RELEASE(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
static uint32_t ropADD_AX_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg = LOAD_REG_W(REG_AX);

        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
        ADD_HOST_REG_IMM_W(host_reg, fetchdat & 0xffff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, fetchdat & 0xffff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ADD16);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_W_RELEASE(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 2;
}
static uint32_t ropADD_EAX_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg = LOAD_REG_L(REG_EAX);

        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
        fetchdat = fastreadl(cs + op_pc);
        ADD_HOST_REG_IMM(host_reg, fetchdat);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, fetchdat);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ADD32);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_L_RELEASE(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 4;
}

static uint32_t ropCMP_AL_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg = LOAD_REG_B(REG_AL);

        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, host_reg);
        host_reg = CMP_HOST_REG_IMM_B(host_reg, fetchdat & 0xff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, fetchdat & 0xff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB8);
        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, host_reg);
        RELEASE_REG(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
static uint32_t ropCMP_AX_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg = LOAD_REG_W(REG_AX);

        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
        host_reg = CMP_HOST_REG_IMM_W(host_reg, fetchdat & 0xffff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, fetchdat & 0xffff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB16);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, host_reg);
        RELEASE_REG(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 2;
}
static uint32_t ropCMP_EAX_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg = LOAD_REG_L(REG_EAX);

        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
        fetchdat = fastreadl(cs + op_pc);
        host_reg = CMP_HOST_REG_IMM_L(host_reg, fetchdat);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, fetchdat);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB32);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, host_reg);
        RELEASE_REG(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 4;
}

static uint32_t ropSUB_AL_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg = LOAD_REG_B(REG_AL);

        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, host_reg);
        SUB_HOST_REG_IMM_B(host_reg, fetchdat & 0xff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, fetchdat & 0xff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB8);
        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_B_RELEASE(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
static uint32_t ropSUB_AX_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg = LOAD_REG_W(REG_AX);

        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
        SUB_HOST_REG_IMM_W(host_reg, fetchdat & 0xffff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, fetchdat & 0xffff);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB16);
        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_W_RELEASE(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 2;
}
static uint32_t ropSUB_EAX_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg = LOAD_REG_L(REG_EAX);

        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
        fetchdat = fastreadl(cs + op_pc);
        SUB_HOST_REG_IMM(host_reg, fetchdat);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, fetchdat);
        STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB32);
        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, host_reg);
        STORE_REG_L_RELEASE(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 4;
}

static uint32_t rop80(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;
        uint32_t imm;
        x86seg *target_seg = NULL;

        if ((fetchdat & 0x30) == 0x10)
                return 0;

        if ((fetchdat & 0xc0) != 0xc0)
        {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                if ((fetchdat & 0x38) == 0x38)
                {
                        MEM_LOAD_ADDR_EA_B(target_seg);
                        host_reg = 0;
                }
                else
                {
                        SAVE_EA();
                        MEM_CHECK_WRITE(target_seg);
                        host_reg = MEM_LOAD_ADDR_EA_B_NO_ABRT(target_seg);
                }
                imm = fastreadb(cs + op_pc + 1);
        }
        else
        {
                host_reg = LOAD_REG_B(fetchdat & 7);
                imm = (fetchdat >> 8) & 0xff;
        }

        switch (fetchdat & 0x38)
        {
                case 0x00: /*ADD*/
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, host_reg);
                ADD_HOST_REG_IMM_B(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ADD8);
                break;
                case 0x08: /*OR*/
                OR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN8);
                break;
                case 0x20: /*AND*/
                AND_HOST_REG_IMM(host_reg, imm | 0xffffff00);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN8);
                break;
                case 0x28: /*SUB*/
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, host_reg);
                SUB_HOST_REG_IMM_B(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB8);
                break;
                case 0x30: /*XOR*/
                XOR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN8);
                break;
                case 0x38: /*CMP*/
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, host_reg);
                host_reg = CMP_HOST_REG_IMM_B(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB8);
                break;
        }

        STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, host_reg);
        if ((fetchdat & 0x38) != 0x38)
        {
                if ((fetchdat & 0xc0) != 0xc0)
                {
                        LOAD_EA();
                        MEM_STORE_ADDR_EA_B_NO_ABRT(target_seg, host_reg);
                }
                else
                {
                        STORE_REG_B_RELEASE(host_reg);
                }
        }
        else
                RELEASE_REG(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 2;
}

static uint32_t rop81_w(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;
        uint32_t imm;
        x86seg *target_seg = NULL;

        if ((fetchdat & 0x30) == 0x10)
                return 0;

        if ((fetchdat & 0xc0) != 0xc0)
        {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                if ((fetchdat & 0x38) == 0x38)
                {
                        MEM_LOAD_ADDR_EA_W(target_seg);
                        host_reg = 0;
                }
                else
                {
                        SAVE_EA();
                        MEM_CHECK_WRITE_W(target_seg);
                        host_reg = MEM_LOAD_ADDR_EA_W_NO_ABRT(target_seg);
                }
                imm = fastreadw(cs + op_pc + 1);
        }
        else
        {
                host_reg = LOAD_REG_W(fetchdat & 7);
                imm = (fetchdat >> 8) & 0xffff;
        }

        switch (fetchdat & 0x38)
        {
                case 0x00: /*ADD*/
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
                ADD_HOST_REG_IMM_W(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ADD16);
                break;
                case 0x08: /*OR*/
                OR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN16);
                break;
                case 0x20: /*AND*/
                AND_HOST_REG_IMM(host_reg, imm | 0xffff0000);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN16);
                break;
                case 0x28: /*SUB*/
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
                SUB_HOST_REG_IMM_W(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB16);
                break;
                case 0x30: /*XOR*/
                XOR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN16);
                break;
                case 0x38: /*CMP*/
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
                host_reg = CMP_HOST_REG_IMM_W(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB16);
                break;
        }

        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, host_reg);
        if ((fetchdat & 0x38) != 0x38)
        {
                if ((fetchdat & 0xc0) != 0xc0)
                {
                        LOAD_EA();
                        MEM_STORE_ADDR_EA_W_NO_ABRT(target_seg, host_reg);
                }
                else
                {
                        STORE_REG_W_RELEASE(host_reg);
                }
        }
        else
                RELEASE_REG(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 3;
}
static uint32_t rop81_l(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;
        uint32_t imm;
        x86seg *target_seg = NULL;

        if ((fetchdat & 0x30) == 0x10)
                return 0;

        if ((fetchdat & 0xc0) != 0xc0)
        {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                if ((fetchdat & 0x38) == 0x38)
                {
                        MEM_LOAD_ADDR_EA_L(target_seg);
                        host_reg = 0;
                }
                else
                {
                        SAVE_EA();
                        MEM_CHECK_WRITE(target_seg);
                        host_reg = MEM_LOAD_ADDR_EA_L_NO_ABRT(target_seg);
                }
        }
        else
        {
                host_reg = LOAD_REG_L(fetchdat & 7);
        }
        imm = fastreadl(cs + op_pc + 1);

        switch (fetchdat & 0x38)
        {
                case 0x00: /*ADD*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
                ADD_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ADD32);
                break;
                case 0x08: /*OR*/
                OR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN32);
                break;
                case 0x20: /*AND*/
                AND_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN32);
                break;
                case 0x28: /*SUB*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
                SUB_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB32);
                break;
                case 0x30: /*XOR*/
                XOR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN32);
                break;
                case 0x38: /*CMP*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
                host_reg = CMP_HOST_REG_IMM_L(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB32);
                break;
        }

        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, host_reg);
        if ((fetchdat & 0x38) != 0x38)
        {
                if ((fetchdat & 0xc0) != 0xc0)
                {
                        LOAD_EA();
                        MEM_STORE_ADDR_EA_L_NO_ABRT(target_seg, host_reg);
                }
                else
                {
                        STORE_REG_L_RELEASE(host_reg);
                }
        }
        else
                RELEASE_REG(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 5;
}

static uint32_t rop83_w(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;
        uint32_t imm;
        x86seg *target_seg = NULL;

        if ((fetchdat & 0x30) == 0x10)
                return 0;

        if ((fetchdat & 0xc0) != 0xc0)
        {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                if ((fetchdat & 0x38) == 0x38)
                {
                        MEM_LOAD_ADDR_EA_W(target_seg);
                        host_reg = 0;
                }
                else
                {
                        SAVE_EA();
                        MEM_CHECK_WRITE_W(target_seg);
                        host_reg = MEM_LOAD_ADDR_EA_W_NO_ABRT(target_seg);
                }
                imm = fastreadb(cs + op_pc + 1);
        }
        else
        {
                host_reg = LOAD_REG_W(fetchdat & 7);
                imm = (fetchdat >> 8) & 0xff;
        }

        if (imm & 0x80)
                imm |= 0xff80;

        switch (fetchdat & 0x38)
        {
                case 0x00: /*ADD*/
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
                ADD_HOST_REG_IMM_W(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ADD16);
                break;
                case 0x08: /*OR*/
                OR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN16);
                break;
                case 0x20: /*AND*/
                AND_HOST_REG_IMM(host_reg, imm | 0xffff0000);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN16);
                break;
                case 0x28: /*SUB*/
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
                SUB_HOST_REG_IMM_W(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB16);
                break;
                case 0x30: /*XOR*/
                XOR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN16);
                break;
                case 0x38: /*CMP*/
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
                host_reg = CMP_HOST_REG_IMM_W(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB16);
                break;
        }

        STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, host_reg);
        if ((fetchdat & 0x38) != 0x38)
        {
                if ((fetchdat & 0xc0) != 0xc0)
                {
                        LOAD_EA();
                        MEM_STORE_ADDR_EA_W_NO_ABRT(target_seg, host_reg);
                }
                else
                {
                        STORE_REG_W_RELEASE(host_reg);
                }
        }
        else
                RELEASE_REG(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 2;
}
static uint32_t rop83_l(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;
        uint32_t imm;
        x86seg *target_seg = NULL;

        if ((fetchdat & 0x30) == 0x10)
                return 0;

        if ((fetchdat & 0xc0) != 0xc0)
        {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                if ((fetchdat & 0x38) == 0x38)
                {
                        MEM_LOAD_ADDR_EA_L(target_seg);
                        host_reg = 0;
                }
                else
                {
                        SAVE_EA();
                        MEM_CHECK_WRITE_L(target_seg);
                        host_reg = MEM_LOAD_ADDR_EA_L_NO_ABRT(target_seg);
                }
                imm = fastreadb(cs + op_pc + 1);
        }
        else
        {
                host_reg = LOAD_REG_L(fetchdat & 7);
                imm = (fetchdat >> 8) & 0xff;
        }

        if (imm & 0x80)
                imm |= 0xffffff80;

        switch (fetchdat & 0x38)
        {
                case 0x00: /*ADD*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
                ADD_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ADD32);
                break;
                case 0x08: /*OR*/
                OR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN32);
                break;
                case 0x20: /*AND*/
                AND_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN32);
                break;
                case 0x28: /*SUB*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
                SUB_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB32);
                break;
                case 0x30: /*XOR*/
                XOR_HOST_REG_IMM(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_ZN32);
                break;
                case 0x38: /*CMP*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
                host_reg = CMP_HOST_REG_IMM_L(host_reg, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, imm);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_SUB32);
                break;
        }

        STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, host_reg);
        if ((fetchdat & 0x38) != 0x38)
        {
                if ((fetchdat & 0xc0) != 0xc0)
                {
                        LOAD_EA();
                        MEM_STORE_ADDR_EA_L_NO_ABRT(target_seg, host_reg);
                }
                else
                {
                        STORE_REG_L_RELEASE(host_reg);
                }
        }
        else
                RELEASE_REG(host_reg);

        codegen_flags_changed = 1;
        return op_pc + 2;
}
