static uint32_t ropMOVQ_q_mm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg1, host_reg2 = 0;

        MMX_ENTER();

        LOAD_MMX_Q((fetchdat >> 3) & 7, &host_reg1, &host_reg2);

        if ((fetchdat & 0xc0) == 0xc0)
        {
                STORE_MMX_Q(fetchdat & 7, host_reg1, host_reg2);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

                CHECK_SEG_WRITE(target_seg);
                CHECK_SEG_LIMITS(target_seg, 7);

                MEM_STORE_ADDR_EA_Q(target_seg, host_reg1, host_reg2);
        }

        return op_pc + 1;
}

static uint32_t ropMOVQ_mm_q(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        MMX_ENTER();

        if ((fetchdat & 0xc0) == 0xc0)
        {
                int host_reg1, host_reg2;

                LOAD_MMX_Q(fetchdat & 7, &host_reg1, &host_reg2);
                STORE_MMX_Q((fetchdat >> 3) & 7, host_reg1, host_reg2);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

                CHECK_SEG_READ(target_seg);

                MEM_LOAD_ADDR_EA_Q(target_seg);
                STORE_MMX_Q((fetchdat >> 3) & 7, LOAD_Q_REG_1, LOAD_Q_REG_2);
        }

        return op_pc + 1;
}

static uint32_t ropMOVD_l_mm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int host_reg;

        MMX_ENTER();

        host_reg = LOAD_MMX_D((fetchdat >> 3) & 7);

        if ((fetchdat & 0xc0) == 0xc0)
        {
                STORE_REG_TARGET_L_RELEASE(host_reg, fetchdat & 7);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

                CHECK_SEG_WRITE(target_seg);
                CHECK_SEG_LIMITS(target_seg, 3);

                MEM_STORE_ADDR_EA_L(target_seg, host_reg);
        }

        return op_pc + 1;
}
static uint32_t ropMOVD_mm_l(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        MMX_ENTER();

        if ((fetchdat & 0xc0) == 0xc0)
        {
                int host_reg = LOAD_REG_L(fetchdat & 7);
                STORE_MMX_LQ((fetchdat >> 3) & 7, host_reg);
        }
        else
        {
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);

                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);

                CHECK_SEG_READ(target_seg);

                MEM_LOAD_ADDR_EA_L(target_seg);
                STORE_MMX_LQ((fetchdat >> 3) & 7, 0);
        }

        return op_pc + 1;
}

#define MMX_OP(name, func)                                                                                      \
static uint32_t name(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)     \
{                                                                                                               \
        int src_reg1, src_reg2;                                                                                 \
        int xmm_src, xmm_dst;                                                                                   \
                                                                                                                \
        MMX_ENTER();                                                                                            \
                                                                                                                \
        if ((fetchdat & 0xc0) == 0xc0)                                                                          \
        {                                                                                                       \
                xmm_src = LOAD_MMX_Q_MMX(fetchdat & 7);                                                         \
        }                                                                                                       \
        else                                                                                                    \
        {                                                                                                       \
                x86seg *target_seg = FETCH_EA(op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32);                    \
                                                                                                                \
                STORE_IMM_ADDR_L((uintptr_t)&cpu_state.oldpc, op_old_pc);                                                 \
                                                                                                                \
                CHECK_SEG_READ(target_seg);                                                                     \
                                                                                                                \
                MEM_LOAD_ADDR_EA_Q(target_seg);                                                                 \
                src_reg1 = LOAD_Q_REG_1;                                                                        \
                src_reg2 = LOAD_Q_REG_2;                                                                        \
                xmm_src = LOAD_INT_TO_MMX(src_reg1, src_reg2);                                                  \
        }                                                                                                       \
        xmm_dst = LOAD_MMX_Q_MMX((fetchdat >> 3) & 7);                                                          \
        func(xmm_dst, xmm_src);                                                                              \
        STORE_MMX_Q_MMX((fetchdat >> 3) & 7, xmm_dst);                                                          \
                                                                                                                \
        return op_pc + 1;                                                                                       \
}

MMX_OP(ropPAND,  MMX_AND)
MMX_OP(ropPANDN, MMX_ANDN)
MMX_OP(ropPOR,   MMX_OR)
MMX_OP(ropPXOR,  MMX_XOR)

MMX_OP(ropPADDB,    MMX_ADDB)
MMX_OP(ropPADDW,    MMX_ADDW)
MMX_OP(ropPADDD,    MMX_ADDD)
MMX_OP(ropPADDSB,   MMX_ADDSB)
MMX_OP(ropPADDSW,   MMX_ADDSW)
MMX_OP(ropPADDUSB,  MMX_ADDUSB)
MMX_OP(ropPADDUSW,  MMX_ADDUSW)

MMX_OP(ropPSUBB,    MMX_SUBB)
MMX_OP(ropPSUBW,    MMX_SUBW)
MMX_OP(ropPSUBD,    MMX_SUBD)
MMX_OP(ropPSUBSB,   MMX_SUBSB)
MMX_OP(ropPSUBSW,   MMX_SUBSW)
MMX_OP(ropPSUBUSB,  MMX_SUBUSB)
MMX_OP(ropPSUBUSW,  MMX_SUBUSW)

MMX_OP(ropPUNPCKLBW, MMX_PUNPCKLBW);
MMX_OP(ropPUNPCKLWD, MMX_PUNPCKLWD);
MMX_OP(ropPUNPCKLDQ, MMX_PUNPCKLDQ);
MMX_OP(ropPACKSSWB,  MMX_PACKSSWB);
MMX_OP(ropPCMPGTB,   MMX_PCMPGTB);
MMX_OP(ropPCMPGTW,   MMX_PCMPGTW);
MMX_OP(ropPCMPGTD,   MMX_PCMPGTD);
MMX_OP(ropPACKUSWB,  MMX_PACKUSWB);
MMX_OP(ropPUNPCKHBW, MMX_PUNPCKHBW);
MMX_OP(ropPUNPCKHWD, MMX_PUNPCKHWD);
MMX_OP(ropPUNPCKHDQ, MMX_PUNPCKHDQ);
MMX_OP(ropPACKSSDW,  MMX_PACKSSDW);

MMX_OP(ropPCMPEQB,   MMX_PCMPEQB);
MMX_OP(ropPCMPEQW,   MMX_PCMPEQW);
MMX_OP(ropPCMPEQD,   MMX_PCMPEQD);

MMX_OP(ropPSRLW,    MMX_PSRLW)
MMX_OP(ropPSRLD,    MMX_PSRLD)
MMX_OP(ropPSRLQ,    MMX_PSRLQ)
MMX_OP(ropPSRAW,    MMX_PSRAW)
MMX_OP(ropPSRAD,    MMX_PSRAD)
MMX_OP(ropPSLLW,    MMX_PSLLW)
MMX_OP(ropPSLLD,    MMX_PSLLD)
MMX_OP(ropPSLLQ,    MMX_PSLLQ)

MMX_OP(ropPMULLW,   MMX_PMULLW);
MMX_OP(ropPMULHW,   MMX_PMULHW);
MMX_OP(ropPMADDWD,  MMX_PMADDWD);

static uint32_t ropPSxxW_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int xmm_dst;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;
        if ((fetchdat & 0x08) || !(fetchdat & 0x30))
                return 0;

        MMX_ENTER();

        xmm_dst = LOAD_MMX_Q_MMX(fetchdat & 7);
        switch (fetchdat & 0x38)
        {
                case 0x10: /*PSRLW*/
                MMX_PSRLW_imm(xmm_dst, (fetchdat >> 8) & 0xff);
                break;
                case 0x20: /*PSRAW*/
                MMX_PSRAW_imm(xmm_dst, (fetchdat >> 8) & 0xff);
                break;
                case 0x30: /*PSLLW*/
                MMX_PSLLW_imm(xmm_dst, (fetchdat >> 8) & 0xff);
                break;
        }
        STORE_MMX_Q_MMX(fetchdat & 7, xmm_dst);

        return op_pc + 2;
}
static uint32_t ropPSxxD_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int xmm_dst;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;
        if ((fetchdat & 0x08) || !(fetchdat & 0x30))
                return 0;

        MMX_ENTER();

        xmm_dst = LOAD_MMX_Q_MMX(fetchdat & 7);
        switch (fetchdat & 0x38)
        {
                case 0x10: /*PSRLD*/
                MMX_PSRLD_imm(xmm_dst, (fetchdat >> 8) & 0xff);
                break;
                case 0x20: /*PSRAD*/
                MMX_PSRAD_imm(xmm_dst, (fetchdat >> 8) & 0xff);
                break;
                case 0x30: /*PSLLD*/
                MMX_PSLLD_imm(xmm_dst, (fetchdat >> 8) & 0xff);
                break;
        }
        STORE_MMX_Q_MMX(fetchdat & 7, xmm_dst);

        return op_pc + 2;
}
static uint32_t ropPSxxQ_imm(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        int xmm_dst;

        if ((fetchdat & 0xc0) != 0xc0)
                return 0;
        if ((fetchdat & 0x08) || !(fetchdat & 0x30))
                return 0;

        MMX_ENTER();

        xmm_dst = LOAD_MMX_Q_MMX(fetchdat & 7);
        switch (fetchdat & 0x38)
        {
                case 0x10: /*PSRLQ*/
                MMX_PSRLQ_imm(xmm_dst, (fetchdat >> 8) & 0xff);
                break;
                case 0x20: /*PSRAQ*/
                MMX_PSRAQ_imm(xmm_dst, (fetchdat >> 8) & 0xff);
                break;
                case 0x30: /*PSLLQ*/
                MMX_PSLLQ_imm(xmm_dst, (fetchdat >> 8) & 0xff);
                break;
        }
        STORE_MMX_Q_MMX(fetchdat & 7, xmm_dst);

        return op_pc + 2;
}

static uint32_t ropEMMS(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block)
{
        codegen_mmx_entered = 0;

        return 0;
}
