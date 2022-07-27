static uint32_t ropNOP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        return op_pc;
}

static uint32_t ropCLD(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        CLEAR_BITS((uintptr_t)&cpu_state.flags, D_FLAG);
        return op_pc;
}
static uint32_t ropSTD(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        SET_BITS((uintptr_t)&cpu_state.flags, D_FLAG);
        return op_pc;
}

static uint32_t ropCLI(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        if (!IOPLp && (cr4 & (CR4_VME | CR4_PVI)))
                return 0;
        CLEAR_BITS((uintptr_t)&cpu_state.flags, I_FLAG);
#ifdef CHECK_INT
        CLEAR_BITS((uintptr_t)&pic_pending, 0xffffffff);
#endif
        return op_pc;
}
static uint32_t ropSTI(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        if (!IOPLp && (cr4 & (CR4_VME | CR4_PVI)))
                return 0;
        SET_BITS((uintptr_t)&cpu_state.flags, I_FLAG);
        return op_pc;
}

static uint32_t ropFE(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg = NULL;
        int host_reg;

        if ((fetchdat & 0x30) != 0x00)
                return 0;

        CALL_FUNC((uintptr_t)flags_rebuild_c);

        if ((fetchdat & 0xc0) == 0xc0)
                host_reg = LOAD_REG_B(fetchdat & 7);
        else
        {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

                SAVE_EA();
                MEM_CHECK_WRITE(target_seg);
                host_reg = MEM_LOAD_ADDR_EA_B_NO_ABRT(target_seg);
        }

        switch (fetchdat & 0x38)
        {
                case 0x00: /*INC*/
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, host_reg);
                ADD_HOST_REG_IMM_B(host_reg, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_INC8);
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, host_reg);
                break;
                case 0x08: /*DEC*/
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_op1, host_reg);
                SUB_HOST_REG_IMM_B(host_reg, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_DEC8);
                STORE_HOST_REG_ADDR_BL((uintptr_t)&cpu_state.flags_res, host_reg);
                break;
        }

        if ((fetchdat & 0xc0) == 0xc0)
                STORE_REG_B_RELEASE(host_reg);
        else
        {
                LOAD_EA();
                MEM_STORE_ADDR_EA_B_NO_ABRT(target_seg, host_reg);
        }
        codegen_flags_changed = 1;

        return op_pc + 1;
}
static uint32_t codegen_temp;
static uint32_t ropFF_16(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg = NULL;
        int host_reg;

        if ((fetchdat & 0x30) != 0x00 && (fetchdat & 0x08))
                return 0;

        if ((fetchdat & 0x30) == 0x00)
                CALL_FUNC((uintptr_t)flags_rebuild_c);

        if ((fetchdat & 0xc0) == 0xc0)
                host_reg = LOAD_REG_W(fetchdat & 7);
        else
        {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

                if ((fetchdat & 0x30) != 0x00)
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
        }

        switch (fetchdat & 0x38)
        {
                case 0x00: /*INC*/
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
                ADD_HOST_REG_IMM_W(host_reg, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_INC16);
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, host_reg);
                if ((fetchdat & 0xc0) == 0xc0)
                        STORE_REG_W_RELEASE(host_reg);
                else
                {
                        LOAD_EA();
                        MEM_STORE_ADDR_EA_W_NO_ABRT(target_seg, host_reg);
                }
                codegen_flags_changed = 1;
                return op_pc + 1;
                case 0x08: /*DEC*/
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_op1, host_reg);
                SUB_HOST_REG_IMM_W(host_reg, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_DEC16);
                STORE_HOST_REG_ADDR_WL((uintptr_t)&cpu_state.flags_res, host_reg);
                if ((fetchdat & 0xc0) == 0xc0)
                        STORE_REG_W_RELEASE(host_reg);
                else
                {
                        LOAD_EA();
                        MEM_STORE_ADDR_EA_W_NO_ABRT(target_seg, host_reg);
                }
                codegen_flags_changed = 1;
                return op_pc + 1;

                case 0x10: /*CALL*/
                STORE_HOST_REG_ADDR_W((uintptr_t)&codegen_temp, host_reg);
                RELEASE_REG(host_reg);

                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                LOAD_STACK_TO_EA(-2);
                host_reg = LOAD_REG_IMM(op_pc + 1);
                MEM_STORE_ADDR_EA_W(&cpu_state.seg_ss, host_reg);
                SP_MODIFY(-2);

                host_reg = LOAD_VAR_W((uintptr_t)&codegen_temp);
                STORE_HOST_REG_ADDR_W((uintptr_t)&cpu_state.pc, host_reg);
                return -1;

                case 0x20: /*JMP*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.pc, host_reg);
                return -1;

                case 0x30: /*PUSH*/
                if (!host_reg)
                        host_reg = LOAD_HOST_REG(host_reg);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                LOAD_STACK_TO_EA(-2);
                MEM_STORE_ADDR_EA_W(&cpu_state.seg_ss, host_reg);
                SP_MODIFY(-2);
                return op_pc + 1;
        }
        return 0;
}
static uint32_t ropFF_32(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        x86seg *target_seg = NULL;
        int host_reg;

        if ((fetchdat & 0x30) != 0x00 && (fetchdat & 0x08))
                return 0;

        if ((fetchdat & 0x30) == 0x00)
                CALL_FUNC((uintptr_t)flags_rebuild_c);

        if ((fetchdat & 0xc0) == 0xc0)
                host_reg = LOAD_REG_L(fetchdat & 7);
        else
        {
                target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

                if ((fetchdat & 0x30) != 0x00)
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
        }

        switch (fetchdat & 0x38)
        {
                case 0x00: /*INC*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
                ADD_HOST_REG_IMM(host_reg, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_INC32);
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, host_reg);
                if ((fetchdat & 0xc0) == 0xc0)
                        STORE_REG_L_RELEASE(host_reg);
                else
                {
                        LOAD_EA();
                        MEM_STORE_ADDR_EA_L_NO_ABRT(target_seg, host_reg);
                }
                codegen_flags_changed = 1;
                return op_pc + 1;
                case 0x08: /*DEC*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_op1, host_reg);
                SUB_HOST_REG_IMM(host_reg, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op2, 1);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.flags_op, FLAGS_DEC32);
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.flags_res, host_reg);
                if ((fetchdat & 0xc0) == 0xc0)
                        STORE_REG_L_RELEASE(host_reg);
                else
                {
                        LOAD_EA();
                        MEM_STORE_ADDR_EA_L_NO_ABRT(target_seg, host_reg);
                }
                codegen_flags_changed = 1;
                return op_pc + 1;

                case 0x10: /*CALL*/
                STORE_HOST_REG_ADDR((uintptr_t)&codegen_temp, host_reg);
                RELEASE_REG(host_reg);

                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                LOAD_STACK_TO_EA(-4);
                host_reg = LOAD_REG_IMM(op_pc + 1);
                MEM_STORE_ADDR_EA_L(&cpu_state.seg_ss, host_reg);
                SP_MODIFY(-4);

                host_reg = LOAD_VAR_L((uintptr_t)&codegen_temp);
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.pc, host_reg);
                return -1;

                case 0x20: /*JMP*/
                STORE_HOST_REG_ADDR((uintptr_t)&cpu_state.pc, host_reg);
                return -1;

                case 0x30: /*PUSH*/
                if (!host_reg)
                        host_reg = LOAD_HOST_REG(host_reg);
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);
                LOAD_STACK_TO_EA(-4);
                MEM_STORE_ADDR_EA_L(&cpu_state.seg_ss, host_reg);
                SP_MODIFY(-4);
                return op_pc + 1;
        }
        return 0;
}
