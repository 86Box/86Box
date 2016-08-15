/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
static uint32_t ropNOP(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        return op_pc;
}

static uint32_t ropCLD(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        CLEAR_BITS((uintptr_t)&flags, D_FLAG);
        return op_pc;
}
static uint32_t ropSTD(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        SET_BITS((uintptr_t)&flags, D_FLAG);
        return op_pc;
}

static uint32_t codegen_temp;
static uint32_t ropFF_16(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;
        
        if ((fetchdat & 0x38) != 0x10 && (fetchdat & 0x38) != 0x20)// && (fetchdat & 0x38) != 0x30)
                return 0;
        
        if ((fetchdat & 0xc0) == 0xc0)
        {
                host_reg = LOAD_REG_W(fetchdat & 7);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_W(target_seg);
                host_reg = 0;
        }
        
        switch (fetchdat & 0x38)
        {
                case 0x10: /*CALL*/
                STORE_HOST_REG_ADDR_W((uintptr_t)&codegen_temp, host_reg);
                RELEASE_REG(host_reg);

                STORE_IMM_ADDR_L((uintptr_t)&oldpc, op_old_pc);
                LOAD_STACK_TO_EA(-2);
                host_reg = LOAD_REG_IMM(op_pc + 1);
                MEM_STORE_ADDR_EA_W(&_ss, host_reg);
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
                STORE_IMM_ADDR_L((uintptr_t)&oldpc, op_old_pc);
                LOAD_STACK_TO_EA(-2);
                MEM_STORE_ADDR_EA_W(&_ss, host_reg);
                SP_MODIFY(-2);
                return op_pc + 1;
        }
}
static uint32_t ropFF_32(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;
        
        if ((fetchdat & 0x38) != 0x10 && (fetchdat & 0x38) != 0x20)// && (fetchdat & 0x38) != 0x30)
                return 0;
        
        if ((fetchdat & 0xc0) == 0xc0)
        {
                host_reg = LOAD_REG_L(fetchdat & 7);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);
                STORE_IMM_ADDR_L((uintptr_t)&oldpc, op_old_pc);
                MEM_LOAD_ADDR_EA_L(target_seg);
                host_reg = 0;
        }
        
        switch (fetchdat & 0x38)
        {
                case 0x10: /*CALL*/
                STORE_HOST_REG_ADDR((uintptr_t)&codegen_temp, host_reg);
                RELEASE_REG(host_reg);

                STORE_IMM_ADDR_L((uintptr_t)&oldpc, op_old_pc);
                LOAD_STACK_TO_EA(-4);
                host_reg = LOAD_REG_IMM(op_pc + 1);
                MEM_STORE_ADDR_EA_L(&_ss, host_reg);
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
                STORE_IMM_ADDR_L((uintptr_t)&oldpc, op_old_pc);
                LOAD_STACK_TO_EA(-4);
                MEM_STORE_ADDR_EA_L(&_ss, host_reg);
                SP_MODIFY(-4);
                return op_pc + 1;
        }
}
