/*Register allocation :
        EBX, ECX, EDX - emulated registers
        EAX - work register, EA storage
        ESI, EDI - work registers
        EBP - points at emulated register array
*/
#define HOST_REG_START 1
#define HOST_REG_END 4
#define HOST_REG_XMM_START 0
#define HOST_REG_XMM_END 7
static inline int find_host_reg()
{
        int c;
        for (c = HOST_REG_START; c < HOST_REG_END; c++)
        {
                if (host_reg_mapping[c] == -1)
                        break;
        }

        if (c == NR_HOST_REGS)
                fatal("Out of host regs!\n");
        return c;
}
static inline int find_host_xmm_reg()
{
        int c;
        for (c = HOST_REG_XMM_START; c < HOST_REG_XMM_END; c++)
        {
                if (host_reg_xmm_mapping[c] == -1)
                        break;
        }

        if (c == HOST_REG_XMM_END)
                fatal("Out of host XMM regs!\n");
        return c;
}

#if 0
static inline void STORE_IMM_ADDR_B(uintptr_t addr, uint8_t val)
{
        addbyte(0xC6); /*MOVB [addr],val*/
        addbyte(0x05);
        addlong(addr);
        addbyte(val);
}
static inline void STORE_IMM_ADDR_W(uintptr_t addr, uint16_t val)
{
        addbyte(0x66); /*MOVW [addr],val*/
        addbyte(0xC7);
        addbyte(0x05);
        addlong(addr);
        addword(val);
}
#endif
static inline void STORE_IMM_ADDR_L(uintptr_t addr, uint32_t val)
{
        if (addr >= (uintptr_t)&cpu_state && addr < ((uintptr_t)&cpu_state)+0x100)
        {
                addbyte(0xC7); /*MOVL [addr],val*/
                addbyte(0x45);
                addbyte(addr - (uint32_t)&cpu_state - 128);
                addlong(val);
        }
        else
        {
                addbyte(0xC7); /*MOVL [addr],val*/
                addbyte(0x05);
                addlong(addr);
                addlong(val);
        }
}

static inline void STORE_IMM_REG_B(int reg, uint8_t val)
{
        addbyte(0xC6); /*MOVB [addr],val*/
        addbyte(0x45);
        if (reg & 4)
                addbyte((uint8_t)cpu_state_offset(regs[reg & 3].b.h));
        else
                addbyte((uint8_t)cpu_state_offset(regs[reg & 3].b.l));
        addbyte(val);
}
static inline void STORE_IMM_REG_W(int reg, uint16_t val)
{
        addbyte(0x66); /*MOVW [addr],val*/
        addbyte(0xC7);
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(regs[reg & 7].w));
        addword(val);
}
static inline void STORE_IMM_REG_L(int reg, uint32_t val)
{
        addbyte(0xC7); /*MOVL [addr],val*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(regs[reg & 7].l));
        addlong(val);
}

static inline int LOAD_REG_B(int reg)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = reg;

        addbyte(0x0f); /*MOVZX B[reg],host_reg*/
        addbyte(0xb6);
        addbyte(0x45 | (host_reg << 3));
        if (reg & 4)
                addbyte((uint8_t)cpu_state_offset(regs[reg & 3].b.h));
        else
                addbyte((uint8_t)cpu_state_offset(regs[reg & 3].b.l));

        return host_reg;
}
static inline int LOAD_REG_W(int reg)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = reg;

        addbyte(0x0f); /*MOVZX W[reg],host_reg*/
        addbyte(0xb7);
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint8_t)cpu_state_offset(regs[reg & 7].w));

        return host_reg;
}
static inline int LOAD_REG_L(int reg)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = reg;

        addbyte(0x8b); /*MOVL host_reg,[reg]*/
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint8_t)cpu_state_offset(regs[reg & 7].l));

        return host_reg;
}

static inline int LOAD_VAR_W(uintptr_t addr)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = 0;

        addbyte(0x66); /*MOVL host_reg,[reg]*/
        addbyte(0x8b);
        addbyte(0x05 | (host_reg << 3));
        addlong((uint32_t)addr);

        return host_reg;
}
static inline int LOAD_VAR_WL(uintptr_t addr)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = 0;

        addbyte(0x0f); /*MOVZX host_reg, [addr]*/
        addbyte(0xb7);
        addbyte(0x05 | (host_reg << 3));
        addlong((uint32_t)addr);

        return host_reg;
}
static inline int LOAD_VAR_L(uintptr_t addr)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = 0;

        addbyte(0x8b); /*MOVL host_reg,[reg]*/
        addbyte(0x05 | (host_reg << 3));
        addlong((uint32_t)addr);

        return host_reg;
}

static inline int LOAD_REG_IMM(uint32_t imm)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = 0;

        addbyte(0xc7); /*MOVL host_reg, imm*/
        addbyte(0xc0 | host_reg);
        addlong(imm);

        return host_reg;
}

static inline int LOAD_HOST_REG(int host_reg)
{
        int new_host_reg = find_host_reg();
        host_reg_mapping[new_host_reg] = 0;

        addbyte(0x89); /*MOV new_host_reg, host_reg*/
        addbyte(0xc0 | (host_reg << 3) | new_host_reg);

        return new_host_reg;
}

static inline void STORE_REG_B_RELEASE(int host_reg)
{
        addbyte(0x88); /*MOVB [reg],host_reg*/
        addbyte(0x45 | (host_reg << 3));
        if (host_reg_mapping[host_reg] & 4)
                addbyte((uint8_t)cpu_state_offset(regs[host_reg_mapping[host_reg] & 3].b.h));
        else
                addbyte((uint8_t)cpu_state_offset(regs[host_reg_mapping[host_reg] & 3].b.l));
        host_reg_mapping[host_reg] = -1;
}
static inline void STORE_REG_W_RELEASE(int host_reg)
{
        addbyte(0x66); /*MOVW [reg],host_reg*/
        addbyte(0x89);
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint8_t)cpu_state_offset(regs[host_reg_mapping[host_reg]].w));
        host_reg_mapping[host_reg] = -1;
}
static inline void STORE_REG_L_RELEASE(int host_reg)
{
        addbyte(0x89); /*MOVL [reg],host_reg*/
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint8_t)cpu_state_offset(regs[host_reg_mapping[host_reg]].l));
        host_reg_mapping[host_reg] = -1;
}

static inline void STORE_REG_TARGET_B_RELEASE(int host_reg, int guest_reg)
{
        addbyte(0x88); /*MOVB [guest_reg],host_reg*/
        addbyte(0x45 | (host_reg << 3));
        if (guest_reg & 4)
                addbyte((uint8_t)cpu_state_offset(regs[guest_reg & 3].b.h));
        else
                addbyte((uint8_t)cpu_state_offset(regs[guest_reg & 3].b.l));
        host_reg_mapping[host_reg] = -1;
}
static inline void STORE_REG_TARGET_W_RELEASE(int host_reg, int guest_reg)
{
        addbyte(0x66); /*MOVW [guest_reg],host_reg*/
        addbyte(0x89);
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint8_t)cpu_state_offset(regs[guest_reg & 7].w));
        host_reg_mapping[host_reg] = -1;
}
static inline void STORE_REG_TARGET_L_RELEASE(int host_reg, int guest_reg)
{
        addbyte(0x89); /*MOVL [guest_reg],host_reg*/
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint8_t)cpu_state_offset(regs[guest_reg & 7].l));
        host_reg_mapping[host_reg] = -1;
}

static inline void RELEASE_REG(int host_reg)
{
        host_reg_mapping[host_reg] = -1;
}

static inline void STORE_HOST_REG_ADDR_W(uintptr_t addr, int host_reg)
{
        if (addr >= (uintptr_t)&cpu_state && addr < ((uintptr_t)&cpu_state)+0x100)
        {
                addbyte(0x66); /*MOVW [addr],host_reg*/
                addbyte(0x89);
                addbyte(0x45 | (host_reg << 3));
                addbyte((uint32_t)addr - (uint32_t)&cpu_state - 128);
        }
        else
        {
                addbyte(0x66); /*MOVL [reg],host_reg*/
                addbyte(0x89);
                addbyte(0x05 | (host_reg << 3));
                addlong(addr);
        }
}
static inline void STORE_HOST_REG_ADDR(uintptr_t addr, int host_reg)
{
        if (addr >= (uintptr_t)&cpu_state && addr < ((uintptr_t)&cpu_state)+0x100)
        {
                addbyte(0x89); /*MOVL [addr],host_reg*/
                addbyte(0x45 | (host_reg << 3));
                addbyte((uint32_t)addr - (uint32_t)&cpu_state - 128);
        }
        else
        {
                addbyte(0x89); /*MOVL [reg],host_reg*/
                addbyte(0x05 | (host_reg << 3));
                addlong(addr);
        }
}
#define STORE_HOST_REG_ADDR_BL STORE_HOST_REG_ADDR
#define STORE_HOST_REG_ADDR_WL STORE_HOST_REG_ADDR

static inline void ADD_HOST_REG_B(int dst_reg, int src_reg)
{
        addbyte(0x00); /*ADDB dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static inline void ADD_HOST_REG_W(int dst_reg, int src_reg)
{
        addbyte(0x66); /*ADDW dst_reg, src_reg*/
        addbyte(0x01);
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static inline void ADD_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x01); /*ADDL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static inline void ADD_HOST_REG_IMM_B(int host_reg, uint8_t imm)
{
        addbyte(0x80); /*ADDB host_reg, imm*/
        addbyte(0xC0 | host_reg);
        addbyte(imm);
}
static inline void ADD_HOST_REG_IMM_W(int host_reg, uint16_t imm)
{
        if (imm < 0x80 || imm >= 0xff80)
        {
                addbyte(0x66); /*ADDW host_reg, imm*/
                addbyte(0x83);
                addbyte(0xC0 | host_reg);
                addbyte(imm & 0xff);
        }
        else
        {
                addbyte(0x66); /*ADDW host_reg, imm*/
                addbyte(0x81);
                addbyte(0xC0 | host_reg);
                addword(imm);
        }
}
static inline void ADD_HOST_REG_IMM(int host_reg, uint32_t imm)
{
        if (imm < 0x80 || imm >= 0xffffff80)
        {
                addbyte(0x83); /*ADDL host_reg, imm*/
                addbyte(0xC0 | host_reg);
                addbyte(imm & 0xff);
        }
        else
        {
                addbyte(0x81); /*ADDL host_reg, imm*/
                addbyte(0xC0 | host_reg);
                addlong(imm);
        }
}

#define AND_HOST_REG_B AND_HOST_REG_L
#define AND_HOST_REG_W AND_HOST_REG_L
static inline void AND_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x21); /*ANDL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static inline void AND_HOST_REG_IMM(int host_reg, uint32_t imm)
{
        if (imm < 0x80 || imm >= 0xffffff80)
        {
                addbyte(0x83); /*ANDL host_reg, imm*/
                addbyte(0xE0 | host_reg);
                addbyte(imm & 0xff);
        }
        else
        {
                addbyte(0x81); /*ANDL host_reg, imm*/
                addbyte(0xE0 | host_reg);
                addlong(imm);
        }
}
static inline int TEST_HOST_REG_B(int dst_reg, int src_reg)
{
        AND_HOST_REG_B(dst_reg, src_reg);

        return dst_reg;
}
static inline int TEST_HOST_REG_W(int dst_reg, int src_reg)
{
        AND_HOST_REG_W(dst_reg, src_reg);

        return dst_reg;
}
static inline int TEST_HOST_REG_L(int dst_reg, int src_reg)
{
        AND_HOST_REG_L(dst_reg, src_reg);

        return dst_reg;
}
static inline int TEST_HOST_REG_IMM(int host_reg, uint32_t imm)
{
        AND_HOST_REG_IMM(host_reg, imm);

        return host_reg;
}

#define OR_HOST_REG_B OR_HOST_REG_L
#define OR_HOST_REG_W OR_HOST_REG_L
static inline void OR_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x09); /*ORL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static inline void OR_HOST_REG_IMM(int host_reg, uint32_t imm)
{
        if (imm < 0x80 || imm >= 0xffffff80)
        {
                addbyte(0x83); /*ORL host_reg, imm*/
                addbyte(0xC8 | host_reg);
                addbyte(imm & 0xff);
        }
        else
        {
                addbyte(0x81); /*ORL host_reg, imm*/
                addbyte(0xC8 | host_reg);
                addlong(imm);
        }
}

static inline void NEG_HOST_REG_B(int reg)
{
        addbyte(0xf6);
        addbyte(0xd8 | reg);
}
static inline void NEG_HOST_REG_W(int reg)
{
        addbyte(0x66);
        addbyte(0xf7);
        addbyte(0xd8 | reg);
}
static inline void NEG_HOST_REG_L(int reg)
{
        addbyte(0xf7);
        addbyte(0xd8 | reg);
}

static inline void SUB_HOST_REG_B(int dst_reg, int src_reg)
{
        addbyte(0x28); /*SUBB dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static inline void SUB_HOST_REG_W(int dst_reg, int src_reg)
{
        addbyte(0x66); /*SUBW dst_reg, src_reg*/
        addbyte(0x29);
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static inline void SUB_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x29); /*SUBL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static inline void SUB_HOST_REG_IMM_B(int host_reg, uint8_t imm)
{
        addbyte(0x80); /*SUBB host_reg, imm*/
        addbyte(0xE8 | host_reg);
        addbyte(imm);
}
static inline void SUB_HOST_REG_IMM_W(int host_reg, uint16_t imm)
{
        if (imm < 0x80 || imm >= 0xff80)
        {
                addbyte(0x66); /*SUBW host_reg, imm*/
                addbyte(0x83);
                addbyte(0xE8 | host_reg);
                addbyte(imm & 0xff);
        }
        else
        {
                addbyte(0x66); /*SUBW host_reg, imm*/
                addbyte(0x81);
                addbyte(0xE8 | host_reg);
                addword(imm);
        }
}
static inline void SUB_HOST_REG_IMM(int host_reg, uint32_t imm)
{
        if (imm < 0x80 || imm >= 0xffffff80)
        {
                addbyte(0x83); /*SUBL host_reg, imm*/
                addbyte(0xE8 | host_reg);
                addbyte(imm);
        }
        else
        {
                addbyte(0x81); /*SUBL host_reg, imm*/
                addbyte(0xE8 | host_reg);
                addlong(imm);
        }
}

static inline void INC_HOST_REG_W(int host_reg)
{
        addbyte(0x66); /*INCW host_reg*/
        addbyte(0x40 | host_reg);
}
static inline void INC_HOST_REG(int host_reg)
{
        addbyte(0x40 | host_reg); /*DECL host_reg*/
}
static inline void DEC_HOST_REG_W(int host_reg)
{
        addbyte(0x66); /*DECW host_reg*/
        addbyte(0x48 | host_reg);
}
static inline void DEC_HOST_REG(int host_reg)
{
        addbyte(0x48 | host_reg); /*DECL host_reg*/
}

static inline int CMP_HOST_REG_B(int dst_reg, int src_reg)
{
        SUB_HOST_REG_B(dst_reg, src_reg);

        return dst_reg;
}
static inline int CMP_HOST_REG_W(int dst_reg, int src_reg)
{
        SUB_HOST_REG_W(dst_reg, src_reg);

        return dst_reg;
}
static inline int CMP_HOST_REG_L(int dst_reg, int src_reg)
{
        SUB_HOST_REG_L(dst_reg, src_reg);

        return dst_reg;
}
static inline int CMP_HOST_REG_IMM_B(int host_reg, uint8_t imm)
{
        SUB_HOST_REG_IMM_B(host_reg, imm);

        return host_reg;
}
static inline int CMP_HOST_REG_IMM_W(int host_reg, uint16_t imm)
{
        SUB_HOST_REG_IMM_W(host_reg, imm);

        return host_reg;
}
static inline int CMP_HOST_REG_IMM_L(int host_reg, uint32_t imm)
{
        SUB_HOST_REG_IMM(host_reg, imm);

        return host_reg;
}

#define XOR_HOST_REG_B XOR_HOST_REG_L
#define XOR_HOST_REG_W XOR_HOST_REG_L
static inline void XOR_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x31); /*XORL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static inline void XOR_HOST_REG_IMM(int host_reg, uint32_t imm)
{
        if (imm < 0x80 || imm >= 0xffffff80)
        {
                addbyte(0x83); /*XORL host_reg, imm*/
                addbyte(0xF0 | host_reg);
                addbyte(imm & 0xff);
        }
        else
        {
                addbyte(0x81); /*XORL host_reg, imm*/
                addbyte(0xF0 | host_reg);
                addlong(imm);
        }
}

static inline void CALL_FUNC(uintptr_t dest)
{
        addbyte(0xE8); /*CALL*/
        addlong(((uintptr_t)dest - (uintptr_t)(&codeblock[block_current].data[block_pos + 4])));
}

static inline void SHL_B_IMM(int reg, int count)
{
        addbyte(0xc0); /*SHL reg, count*/
        addbyte(0xc0 | reg | 0x20);
        addbyte(count);
}
static inline void SHL_W_IMM(int reg, int count)
{
        addbyte(0x66); /*SHL reg, count*/
        addbyte(0xc1);
        addbyte(0xc0 | reg | 0x20);
        addbyte(count);
}
static inline void SHL_L_IMM(int reg, int count)
{
        addbyte(0xc1); /*SHL reg, count*/
        addbyte(0xc0 | reg | 0x20);
        addbyte(count);
}
static inline void SHR_B_IMM(int reg, int count)
{
        addbyte(0xc0); /*SHR reg, count*/
        addbyte(0xc0 | reg | 0x28);
        addbyte(count);
}
static inline void SHR_W_IMM(int reg, int count)
{
        addbyte(0x66); /*SHR reg, count*/
        addbyte(0xc1);
        addbyte(0xc0 | reg | 0x28);
        addbyte(count);
}
static inline void SHR_L_IMM(int reg, int count)
{
        addbyte(0xc1); /*SHR reg, count*/
        addbyte(0xc0 | reg | 0x28);
        addbyte(count);
}
static inline void SAR_B_IMM(int reg, int count)
{
        addbyte(0xc0); /*SAR reg, count*/
        addbyte(0xc0 | reg | 0x38);
        addbyte(count);
}
static inline void SAR_W_IMM(int reg, int count)
{
        addbyte(0x66); /*SAR reg, count*/
        addbyte(0xc1);
        addbyte(0xc0 | reg | 0x38);
        addbyte(count);
}
static inline void SAR_L_IMM(int reg, int count)
{
        addbyte(0xc1); /*SAR reg, count*/
        addbyte(0xc0 | reg | 0x38);
        addbyte(count);
}


static inline void CHECK_SEG_READ(x86seg *seg)
{
        /*Segments always valid in real/V86 mode*/
        if (!(cr0 & 1) || (cpu_state.eflags & VM_FLAG))
                return;
        /*CS and SS must always be valid*/
        if (seg == &cpu_state.seg_cs || seg == &cpu_state.seg_ss)
                return;
        if (seg->checked)
                return;
        if (seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS))
                return;

        addbyte(0x83); /*CMP seg->base, -1*/
        addbyte(0x05|0x38);
        addlong((uint32_t)&seg->base);
        addbyte(-1);
        addbyte(0x0f);
        addbyte(0x84); /*JE BLOCK_GPF_OFFSET*/
        addlong(BLOCK_GPF_OFFSET - (block_pos + 4));

        seg->checked = 1;
}
static inline void CHECK_SEG_WRITE(x86seg *seg)
{
        /*Segments always valid in real/V86 mode*/
        if (!(cr0 & 1) || (cpu_state.eflags & VM_FLAG))
                return;
        /*CS and SS must always be valid*/
        if (seg == &cpu_state.seg_cs || seg == &cpu_state.seg_ss)
                return;
        if (seg->checked)
                return;
        if (seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS))
                return;

        addbyte(0x83); /*CMP seg->base, -1*/
        addbyte(0x05|0x38);
        addlong((uint32_t)&seg->base);
        addbyte(-1);
        addbyte(0x0f);
        addbyte(0x84); /*JE BLOCK_GPF_OFFSET*/
        addlong(BLOCK_GPF_OFFSET - (block_pos + 4));

        seg->checked = 1;
}
static inline void CHECK_SEG_LIMITS(x86seg *seg, int end_offset)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
                return;

        addbyte(0x3b); /*CMP EAX, seg->limit_low*/
        addbyte(0x05);
        addlong((uint32_t)&seg->limit_low);
        addbyte(0x0f); /*JB BLOCK_GPF_OFFSET*/
        addbyte(0x82);
        addlong(BLOCK_GPF_OFFSET - (block_pos + 4));
        if (end_offset)
        {
                addbyte(0x83); /*ADD EAX, end_offset*/
                addbyte(0xc0);
                addbyte(end_offset);
                addbyte(0x3b); /*CMP EAX, seg->limit_high*/
                addbyte(0x05);
                addlong((uint32_t)&seg->limit_high);
                addbyte(0x0f); /*JNBE BLOCK_GPF_OFFSET*/
                addbyte(0x87);
                addlong(BLOCK_GPF_OFFSET - (block_pos + 4));
                addbyte(0x83); /*SUB EAX, end_offset*/
                addbyte(0xe8);
                addbyte(end_offset);
        }
}

static inline void MEM_LOAD_ADDR_EA_B(x86seg *seg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR EDX, EDX*/
                addbyte(0xd2);
        }
        else
        {
                addbyte(0x8b); /*MOVL EDX, seg->base*/
                addbyte(0x05 | (REG_EDX << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_load_addr_ea_b*/
        addlong(mem_load_addr_ea_b - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));

        host_reg_mapping[0] = 8;
}
static inline int MEM_LOAD_ADDR_EA_B_NO_ABRT(x86seg *seg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR EDX, EDX*/
                addbyte(0xd2);
        }
        else
        {
                addbyte(0x8b); /*MOVL EDX, seg->base*/
                addbyte(0x05 | (REG_EDX << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_load_addr_ea_b_no_abrt*/
        addlong(mem_load_addr_ea_b_no_abrt - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));

        host_reg_mapping[REG_ECX] = 8;

        return REG_ECX;
}
static inline void MEM_LOAD_ADDR_EA_W(x86seg *seg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR EDX, EDX*/
                addbyte(0xd2);
        }
        else
        {
                addbyte(0x8b); /*MOVL EDX, seg->base*/
                addbyte(0x05 | (REG_EDX << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_load_addr_ea_w*/
        addlong(mem_load_addr_ea_w - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));

        host_reg_mapping[0] = 8;
}
static inline void MEM_LOAD_ADDR_EA_W_OFFSET(x86seg *seg, int offset)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR EDX, EDX*/
                addbyte(0xd2);
        }
        else
        {
                addbyte(0x8b); /*MOVL EDX, seg->base*/
                addbyte(0x05 | (REG_EDX << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0x83); /*ADD EAX, offset*/
        addbyte(0xc0);
        addbyte(offset);
        addbyte(0xe8); /*CALL mem_load_addr_ea_w*/
        addlong(mem_load_addr_ea_w - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));

        host_reg_mapping[0] = 8;
}
static inline int MEM_LOAD_ADDR_EA_W_NO_ABRT(x86seg *seg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR EDX, EDX*/
                addbyte(0xd2);
        }
        else
        {
                addbyte(0x8b); /*MOVL EDX, seg->base*/
                addbyte(0x05 | (REG_EDX << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_load_addr_ea_w_no_abrt*/
        addlong(mem_load_addr_ea_w_no_abrt - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));

        host_reg_mapping[REG_ECX] = 8;

        return REG_ECX;
}
static inline void MEM_LOAD_ADDR_EA_L(x86seg *seg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR EDX, EDX*/
                addbyte(0xd2);
        }
        else
        {
                addbyte(0x8b); /*MOVL EDX, seg->base*/
                addbyte(0x05 | (REG_EDX << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_load_addr_ea_l*/
        addlong(mem_load_addr_ea_l - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));


        host_reg_mapping[0] = 8;
}
static inline int MEM_LOAD_ADDR_EA_L_NO_ABRT(x86seg *seg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR EDX, EDX*/
                addbyte(0xd2);
        }
        else
        {
                addbyte(0x8b); /*MOVL EDX, seg->base*/
                addbyte(0x05 | (REG_EDX << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_load_addr_ea_l_no_abrt*/
        addlong(mem_load_addr_ea_l_no_abrt - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));

        host_reg_mapping[REG_ECX] = 8;

        return REG_ECX;
}

static inline void MEM_LOAD_ADDR_EA_Q(x86seg *seg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR EDX, EDX*/
                addbyte(0xd2);
        }
        else
        {
                addbyte(0x8b); /*MOVL EDX, seg->base*/
                addbyte(0x05 | (REG_EDX << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_load_addr_ea_q*/
        addlong(mem_load_addr_ea_q - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));

        host_reg_mapping[0] = 8;
}

static inline void MEM_LOAD_ADDR_IMM_B(x86seg *seg, uint32_t addr)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_LOAD_ADDR_EA_B(seg);
}
static inline void MEM_LOAD_ADDR_IMM_W(x86seg *seg, uint32_t addr)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_LOAD_ADDR_EA_W(seg);
}
static inline void MEM_LOAD_ADDR_IMM_L(x86seg *seg, uint32_t addr)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_LOAD_ADDR_EA_L(seg);
}

static inline void MEM_STORE_ADDR_EA_B(x86seg *seg, int host_reg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        if (host_reg != REG_ECX)
        {
                addbyte(0x89); /*MOV ECX, host_reg*/
                addbyte(0xc0 | REG_ECX | (host_reg << 3));
        }
        addbyte(0xe8); /*CALL mem_store_addr_ea_b*/
        addlong(mem_store_addr_ea_b - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
}
static inline void MEM_STORE_ADDR_EA_B_NO_ABRT(x86seg *seg, int host_reg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        if (host_reg != REG_ECX)
        {
                addbyte(0x89); /*MOV ECX, host_reg*/
                addbyte(0xc0 | REG_ECX | (host_reg << 3));
        }
        addbyte(0xe8); /*CALL mem_store_addr_ea_b_no_abrt*/
        addlong(mem_store_addr_ea_b_no_abrt - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
}
static inline void MEM_STORE_ADDR_EA_W(x86seg *seg, int host_reg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        if (host_reg != REG_ECX)
        {
                addbyte(0x89); /*MOV ECX, host_reg*/
                addbyte(0xc0 | REG_ECX | (host_reg << 3));
        }
        addbyte(0xe8); /*CALL mem_store_addr_ea_w*/
        addlong(mem_store_addr_ea_w - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
}
static inline void MEM_STORE_ADDR_EA_W_NO_ABRT(x86seg *seg, int host_reg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        if (host_reg != REG_ECX)
        {
                addbyte(0x89); /*MOV ECX, host_reg*/
                addbyte(0xc0 | REG_ECX | (host_reg << 3));
        }
        addbyte(0xe8); /*CALL mem_store_addr_ea_w_no_abrt*/
        addlong(mem_store_addr_ea_w_no_abrt - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
}
static inline void MEM_STORE_ADDR_EA_L(x86seg *seg, int host_reg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        if (host_reg != REG_ECX)
        {
                addbyte(0x89); /*MOV ECX, host_reg*/
                addbyte(0xc0 | REG_ECX | (host_reg << 3));
        }
        addbyte(0xe8); /*CALL mem_store_addr_ea_l*/
        addlong(mem_store_addr_ea_l - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
}
static inline void MEM_STORE_ADDR_EA_L_NO_ABRT(x86seg *seg, int host_reg)
{
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        if (host_reg != REG_ECX)
        {
                addbyte(0x89); /*MOV ECX, host_reg*/
                addbyte(0xc0 | REG_ECX | (host_reg << 3));
        }
        addbyte(0xe8); /*CALL mem_store_addr_ea_l_no_abrt*/
        addlong(mem_store_addr_ea_l_no_abrt - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
}
static inline void MEM_STORE_ADDR_EA_Q(x86seg *seg, int host_reg, int host_reg2)
{
        if (host_reg != REG_EBX)
        {
                addbyte(0x89); /*MOV EBX, host_reg*/
                addbyte(0xc0 | REG_EBX | (host_reg << 3));
        }
        if (host_reg2 != REG_ECX)
        {
                addbyte(0x89); /*MOV ECX, host_reg2*/
                addbyte(0xc0 | REG_ECX | (host_reg2 << 3));
        }
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_store_addr_ea_q*/
        addlong(mem_store_addr_ea_q - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
}

static inline void MEM_STORE_ADDR_IMM_B(x86seg *seg, uint32_t addr, int host_reg)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_STORE_ADDR_EA_B(seg, host_reg);
}
static inline void MEM_STORE_ADDR_IMM_L(x86seg *seg, uint32_t addr, int host_reg)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_STORE_ADDR_EA_L(seg, host_reg);
}
static inline void MEM_STORE_ADDR_IMM_W(x86seg *seg, uint32_t addr, int host_reg)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_STORE_ADDR_EA_W(seg, host_reg);
}


static inline x86seg *FETCH_EA_16(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc)
{
        int mod = (fetchdat >> 6) & 3;
        int rm = fetchdat & 7;
        if (!mod && rm == 6)
        {
                addbyte(0xb8); /*MOVL EAX, imm16*/
                addlong((fetchdat >> 8) & 0xffff);
                (*op_pc) += 2;
        }
        else
        {
                switch (mod)
                {
                        case 0:
                        addbyte(0xa1); /*MOVL EAX, *mod1add[0][rm]*/
                        addlong((uint32_t)mod1add[0][rm]);
                        if (mod1add[1][rm] != &zero)
                        {
                                addbyte(0x03); /*ADDL EAX, *mod1add[1][rm]*/
                                addbyte(0x05);
                                addlong((uint32_t)mod1add[1][rm]);
                        }
                        break;
                        case 1:
                        addbyte(0xa1); /*MOVL EAX, *mod1add[0][rm]*/
                        addlong((uint32_t)mod1add[0][rm]);
                        addbyte(0x83); /*ADDL EAX, imm8*/
                        addbyte(0xc0 | REG_EAX);
                        addbyte((int8_t)(rmdat >> 8));
                        if (mod1add[1][rm] != &zero)
                        {
                                addbyte(0x03); /*ADDL EAX, *mod1add[1][rm]*/
                                addbyte(0x05);
                                addlong((uint32_t)mod1add[1][rm]);
                        }
                        (*op_pc)++;
                        break;
                        case 2:
                        addbyte(0xb8); /*MOVL EAX, imm16*/
                        addlong((fetchdat >> 8) & 0xffff);
                        addbyte(0x03); /*ADDL EAX, *mod1add[0][rm]*/
                        addbyte(0x05);
                        addlong((uint32_t)mod1add[0][rm]);
                        if (mod1add[1][rm] != &zero)
                        {
                                addbyte(0x03); /*ADDL EAX, *mod1add[1][rm]*/
                                addbyte(0x05);
                                addlong((uint32_t)mod1add[1][rm]);
                        }
                        (*op_pc) += 2;
                        break;
                }
                addbyte(0x25); /*ANDL EAX, 0xffff*/
                addlong(0xffff);

                if (mod1seg[rm] == &ss && !op_ssegs)
                        op_ea_seg = &cpu_state.seg_ss;
        }
        return op_ea_seg;
}

static inline x86seg *FETCH_EA_32(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, int stack_offset)
{
        uint32_t new_eaaddr;
        int mod = (fetchdat >> 6) & 3;
        int rm = fetchdat & 7;

        if (rm == 4)
        {
                uint8_t sib = fetchdat >> 8;
                (*op_pc)++;

                switch (mod)
                {
                        case 0:
                        if ((sib & 7) == 5)
                        {
                                new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                                addbyte(0xb8); /*MOVL EAX, imm32*/
                                addlong(new_eaaddr);
                                (*op_pc) += 4;
                        }
                        else
                        {
                                addbyte(0x8b); /*MOVL EAX, regs[sib&7].l*/
                                addbyte(0x45);
                                addbyte((uint8_t)cpu_state_offset(regs[sib & 7].l));
                        }
                        break;
                        case 1:
                        addbyte(0x8b); /*MOVL EAX, regs[sib&7].l*/
                        addbyte(0x45);
                        addbyte((uint8_t)cpu_state_offset(regs[sib & 7].l));
                        addbyte(0x83); /*ADDL EAX, imm8*/
                        addbyte(0xc0 | REG_EAX);
                        addbyte((int8_t)(rmdat >> 16));
                        (*op_pc)++;
                        break;
                        case 2:
                        new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                        addbyte(0xb8); /*MOVL EAX, new_eaaddr*/
                        addlong(new_eaaddr);
                        addbyte(0x03); /*ADDL EAX, regs[sib&7].l*/
                        addbyte(0x45);
                        addbyte((uint8_t)cpu_state_offset(regs[sib & 7].l));
                        (*op_pc) += 4;
                        break;
                }
                if (stack_offset && (sib & 7) == 4 && (mod || (sib & 7) != 5)) /*ESP*/
                {
                        if (stack_offset < 0x80 || stack_offset >= 0xffffff80)
                        {
                                addbyte(0x83);
                                addbyte(0xc0 | REG_EAX);
                                addbyte(stack_offset);
                        }
                        else
                        {
                                addbyte(0x05); /*ADDL EAX, stack_offset*/
                                addlong(stack_offset);
                        }
                }
                if (((sib & 7) == 4 || (mod && (sib & 7) == 5)) && !op_ssegs)
                        op_ea_seg = &cpu_state.seg_ss;
                if (((sib >> 3) & 7) != 4)
                {
                        switch (sib >> 6)
                        {
                                case 0:
                                addbyte(0x03); /*ADDL EAX, regs[sib&7].l*/
                                addbyte(0x45);
                                addbyte((uint8_t)cpu_state_offset(regs[(sib >> 3) & 7].l));
                                break;
                                case 1:
                                addbyte(0x8B); addbyte(0x45 | (REG_EDI << 3)); addbyte((uint8_t)cpu_state_offset(regs[(sib >> 3) & 7].l)); /*MOVL EDI, reg*/
                                addbyte(0x01); addbyte(0xc0 | REG_EAX | (REG_EDI << 3)); /*ADDL EAX, EDI*/
                                addbyte(0x01); addbyte(0xc0 | REG_EAX | (REG_EDI << 3)); /*ADDL EAX, EDI*/
                                break;
                                case 2:
                                addbyte(0x8B); addbyte(0x45 | (REG_EDI << 3)); addbyte((uint8_t)cpu_state_offset(regs[(sib >> 3) & 7].l)); /*MOVL EDI, reg*/
                                addbyte(0xC1); addbyte(0xE0 | REG_EDI); addbyte(2); /*SHL EDI, 2*/
                                addbyte(0x01); addbyte(0xc0 | REG_EAX | (REG_EDI << 3)); /*ADDL EAX, EDI*/
                                break;
                                case 3:
                                addbyte(0x8B); addbyte(0x45 | (REG_EDI << 3)); addbyte((uint8_t)cpu_state_offset(regs[(sib >> 3) & 7].l)); /*MOVL EDI reg*/
                                addbyte(0xC1); addbyte(0xE0 | REG_EDI); addbyte(3); /*SHL EDI, 3*/
                                addbyte(0x01); addbyte(0xc0 | REG_EAX | (REG_EDI << 3)); /*ADDL EAX, EDI*/
                                break;
                        }
                }
        }
        else
        {
                if (!mod && rm == 5)
                {
                        new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                        addbyte(0xb8); /*MOVL EAX, imm32*/
                        addlong(new_eaaddr);
                        (*op_pc) += 4;
                        return op_ea_seg;
                }
                addbyte(0x8b); /*MOVL EAX, regs[rm].l*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(regs[rm].l));
                cpu_state.eaaddr = cpu_state.regs[rm].l;
                if (mod)
                {
                        if (rm == 5 && !op_ssegs)
                                op_ea_seg = &cpu_state.seg_ss;
                        if (mod == 1)
                        {
                                addbyte(0x83); /*ADD EAX, imm8*/
                                addbyte(0xc0 | REG_EAX);
                                addbyte((int8_t)(fetchdat >> 8));
                                (*op_pc)++;
                        }
                        else
                        {
                                new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                                addbyte(0x05); /*ADD EAX, imm32*/
                                addlong(new_eaaddr);
                                (*op_pc) += 4;
                        }
                }
        }
        return op_ea_seg;
}

static inline x86seg *FETCH_EA(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, uint32_t op_32)
{
        if (op_32 & 0x200)
                return FETCH_EA_32(op_ea_seg, fetchdat, op_ssegs, op_pc, 0);
        return FETCH_EA_16(op_ea_seg, fetchdat, op_ssegs, op_pc);
}


static inline void LOAD_STACK_TO_EA(int off)
{
        if (stack32)
        {
                addbyte(0x8b); /*MOVL EAX,[ESP]*/
                addbyte(0x45 | (REG_EAX << 3));
                addbyte((uint8_t)cpu_state_offset(regs[REG_ESP].l));
                if (off)
                {
                        addbyte(0x83); /*ADD EAX, off*/
                        addbyte(0xc0 | (0 << 3) | REG_EAX);
                        addbyte(off);
                }
        }
        else
        {
                addbyte(0x0f); /*MOVZX EAX,W[ESP]*/
                addbyte(0xb7);
                addbyte(0x45 | (REG_EAX << 3));
                addbyte((uint8_t)cpu_state_offset(regs[REG_ESP].w));
                if (off)
                {
                        addbyte(0x66); /*ADD AX, off*/
                        addbyte(0x05);
                        addword(off);
                }
        }
}

static inline void LOAD_EBP_TO_EA(int off)
{
        if (stack32)
        {
                addbyte(0x8b); /*MOVL EAX,[EBP]*/
                addbyte(0x45 | (REG_EAX << 3));
                addbyte((uint8_t)cpu_state_offset(regs[REG_EBP].l));
                if (off)
                {
                        addbyte(0x83); /*ADD EAX, off*/
                        addbyte(0xc0 | (0 << 3) | REG_EAX);
                        addbyte(off);
                }
        }
        else
        {
                addbyte(0x0f); /*MOVZX EAX,W[EBP]*/
                addbyte(0xb7);
                addbyte(0x45 | (REG_EAX << 3));
                addbyte((uint8_t)cpu_state_offset(regs[REG_EBP].w));
                if (off)
                {
                        addbyte(0x66); /*ADD AX, off*/
                        addbyte(0x05);
                        addword(off);
                }
        }
}

static inline void SP_MODIFY(int off)
{
        if (stack32)
        {
                if (off < 0x80)
                {
                        addbyte(0x83); /*ADD [ESP], off*/
                        addbyte(0x45);
                        addbyte((uint8_t)cpu_state_offset(regs[REG_ESP].l));
                        addbyte(off);
                }
                else
                {
                        addbyte(0x81); /*ADD [ESP], off*/
                        addbyte(0x45);
                        addbyte((uint8_t)cpu_state_offset(regs[REG_ESP].l));
                        addlong(off);
                }
        }
        else
        {
                if (off < 0x80)
                {
                        addbyte(0x66); /*ADD [SP], off*/
                        addbyte(0x83);
                        addbyte(0x45);
                        addbyte((uint8_t)cpu_state_offset(regs[REG_ESP].w));
                        addbyte(off);
                }
                else
                {
                        addbyte(0x66); /*ADD [SP], off*/
                        addbyte(0x81);
                        addbyte(0x45);
                        addbyte((uint8_t)cpu_state_offset(regs[REG_ESP].w));
                        addword(off);
                }
        }
}


static inline void TEST_ZERO_JUMP_W(int host_reg, uint32_t new_pc, int taken_cycles)
{
        addbyte(0x66); /*CMPW host_reg, 0*/
        addbyte(0x83);
        addbyte(0xc0 | 0x38 | host_reg);
        addbyte(0);
        addbyte(0x75); /*JNZ +*/
        addbyte(7+5+(taken_cycles ? 4 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(pc));
        addlong(new_pc);
        if (taken_cycles)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x6d);
                addbyte((uint8_t)cpu_state_offset(_cycles));
                addbyte(taken_cycles);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}
static inline void TEST_ZERO_JUMP_L(int host_reg, uint32_t new_pc, int taken_cycles)
{
        addbyte(0x83); /*CMPW host_reg, 0*/
        addbyte(0xc0 | 0x38 | host_reg);
        addbyte(0);
        addbyte(0x75); /*JNZ +*/
        addbyte(7+5+(taken_cycles ? 4 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(pc));
        addlong(new_pc);
        if (taken_cycles)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x6d);
                addbyte((uint8_t)cpu_state_offset(_cycles));
                addbyte(taken_cycles);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}

static inline void TEST_NONZERO_JUMP_W(int host_reg, uint32_t new_pc, int taken_cycles)
{
        addbyte(0x66); /*CMPW host_reg, 0*/
        addbyte(0x83);
        addbyte(0xc0 | 0x38 | host_reg);
        addbyte(0);
        addbyte(0x74); /*JZ +*/
        addbyte(7+5+(taken_cycles ? 4 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(pc));
        addlong(new_pc);
        if (taken_cycles)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x6d);
                addbyte((uint8_t)cpu_state_offset(_cycles));
                addbyte(taken_cycles);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}
static inline void TEST_NONZERO_JUMP_L(int host_reg, uint32_t new_pc, int taken_cycles)
{
        addbyte(0x83); /*CMPW host_reg, 0*/
        addbyte(0xc0 | 0x38 | host_reg);
        addbyte(0);
        addbyte(0x74); /*JZ +*/
        addbyte(7+5+(taken_cycles ? 4 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(pc));
        addlong(new_pc);
        if (taken_cycles)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x6d);
                addbyte((uint8_t)cpu_state_offset(_cycles));
                addbyte(taken_cycles);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}

static inline void BRANCH_COND_BE(int pc_offset, uint32_t op_pc, uint32_t offset, int not)
{
        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_SUB8:
                addbyte(0x8a); /*MOV AL, flags_op1*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op1));
                addbyte(0x3a); /*CMP AL, flags_op2*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op2));
                if (not)
                        addbyte(0x76); /*JBE*/
                else
                        addbyte(0x77); /*JNBE*/
                break;
                case FLAGS_SUB16:
                addbyte(0x66); /*MOV AX, flags_op1*/
                addbyte(0x8b);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op1));
                addbyte(0x66); /*CMP AX, flags_op2*/
                addbyte(0x3b);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op2));
                if (not)
                        addbyte(0x76); /*JBE*/
                else
                        addbyte(0x77); /*JNBE*/
                break;
                case FLAGS_SUB32:
                addbyte(0x8b); /*MOV EAX, flags_op1*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op1));
                addbyte(0x3b); /*CMP EAX, flags_op2*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op2));
                if (not)
                        addbyte(0x76); /*JBE*/
                else
                        addbyte(0x77); /*JNBE*/
                break;

                default:
                if (codegen_flags_changed && cpu_state.flags_op != FLAGS_UNKNOWN)
                {
                        addbyte(0x83); /*CMP flags_res, 0*/
                        addbyte(0x7d);
                        addbyte((uint8_t)cpu_state_offset(flags_res));
                        addbyte(0);
                        addbyte(0x74); /*JZ +*/
                }
                else
                {
                        CALL_FUNC((uintptr_t)ZF_SET);
                        addbyte(0x85); /*TEST EAX,EAX*/
                        addbyte(0xc0);
                        addbyte(0x75); /*JNZ +*/
                }
                if (not)
                        addbyte(5+2+2+7+5+(timing_bt ? 4 : 0));
                else
                        addbyte(5+2+2);
                CALL_FUNC((uintptr_t)CF_SET);
                addbyte(0x85); /*TEST EAX,EAX*/
                addbyte(0xc0);
                if (not)
                        addbyte(0x75); /*JNZ +*/
                else
                        addbyte(0x74); /*JZ +*/
                break;
        }
        addbyte(7+5+(timing_bt ? 4 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(pc));
        addlong(op_pc+pc_offset+offset);
        if (timing_bt)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x6d);
                addbyte((uint8_t)cpu_state_offset(_cycles));
                addbyte(timing_bt);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}

static inline void BRANCH_COND_L(int pc_offset, uint32_t op_pc, uint32_t offset, int not)
{
        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_SUB8:
                addbyte(0x8a); /*MOV AL, flags_op1*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op1));
                addbyte(0x3a); /*CMP AL, flags_op2*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op2));
                if (not)
                        addbyte(0x7c); /*JL*/
                else
                        addbyte(0x7d); /*JNL*/
                break;
                case FLAGS_SUB16:
                addbyte(0x66); /*MOV AX, flags_op1*/
                addbyte(0x8b);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op1));
                addbyte(0x66); /*CMP AX, flags_op2*/
                addbyte(0x3b);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op2));
                if (not)
                        addbyte(0x7c); /*JL*/
                else
                        addbyte(0x7d); /*JNL*/
                break;
                case FLAGS_SUB32:
                addbyte(0x8b); /*MOV EAX, flags_op1*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op1));
                addbyte(0x3b); /*CMP EAX, flags_op2*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op2));
                if (not)
                        addbyte(0x7c); /*JL*/
                else
                        addbyte(0x7d); /*JNL*/
                break;

                default:
                CALL_FUNC((uintptr_t)NF_SET);
                addbyte(0x85); /*TEST EAX,EAX*/
                addbyte(0xc0);
                addbyte(0x0f); /*SETNE BL*/
                addbyte(0x95);
                addbyte(0xc3);
                CALL_FUNC((uintptr_t)VF_SET);
                addbyte(0x85); /*TEST EAX,EAX*/
                addbyte(0xc0);
                addbyte(0x0f); /*SETNE AL*/
                addbyte(0x95);
                addbyte(0xc0);
                addbyte(0x38); /*CMP AL, BL*/
                addbyte(0xd8);
                if (not)
                        addbyte(0x75); /*JNZ +*/
                else
                        addbyte(0x74); /*JZ +*/
                break;
        }
        addbyte(7+5+(timing_bt ? 4 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(pc));
        addlong(op_pc+pc_offset+offset);
        if (timing_bt)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x6d);
                addbyte((uint8_t)cpu_state_offset(_cycles));
                addbyte(timing_bt);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}

static inline void BRANCH_COND_LE(int pc_offset, uint32_t op_pc, uint32_t offset, int not)
{
        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_SUB8:
                addbyte(0x8a); /*MOV AL, flags_op1*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op1));
                addbyte(0x3a); /*CMP AL, flags_op2*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op2));
                if (not)
                        addbyte(0x7e); /*JLE*/
                else
                        addbyte(0x7f); /*JNLE*/
                break;
                case FLAGS_SUB16:
                addbyte(0x66); /*MOV AX, flags_op1*/
                addbyte(0x8b);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op1));
                addbyte(0x66); /*CMP AX, flags_op2*/
                addbyte(0x3b);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op2));
                if (not)
                        addbyte(0x7e); /*JLE*/
                else
                        addbyte(0x7f); /*JNLE*/
                break;
                case FLAGS_SUB32:
                addbyte(0x8b); /*MOV EAX, flags_op1*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op1));
                addbyte(0x3b); /*CMP EAX, flags_op2*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(flags_op2));
                if (not)
                        addbyte(0x7e); /*JLE*/
                else
                        addbyte(0x7f); /*JNLE*/
                break;

                default:
                if (codegen_flags_changed && cpu_state.flags_op != FLAGS_UNKNOWN)
                {
                        addbyte(0x83); /*CMP flags_res, 0*/
                        addbyte(0x7d);
                        addbyte((uint8_t)cpu_state_offset(flags_res));
                        addbyte(0);
                        addbyte(0x74); /*JZ +*/
                }
                else
                {
                        CALL_FUNC((uintptr_t)ZF_SET);
                        addbyte(0x85); /*TEST EAX,EAX*/
                        addbyte(0xc0);
                        addbyte(0x75); /*JNZ +*/
                }
                if (not)
                        addbyte(5+2+3+5+2+3+2+2+7+5+(timing_bt ? 4 : 0));
                else
                        addbyte(5+2+3+5+2+3+2+2);

                CALL_FUNC((uintptr_t)NF_SET);
                addbyte(0x85); /*TEST EAX,EAX*/
                addbyte(0xc0);
                addbyte(0x0f); /*SETNE BL*/
                addbyte(0x95);
                addbyte(0xc3);
                CALL_FUNC((uintptr_t)VF_SET);
                addbyte(0x85); /*TEST EAX,EAX*/
                addbyte(0xc0);
                addbyte(0x0f); /*SETNE AL*/
                addbyte(0x95);
                addbyte(0xc0);
                addbyte(0x38); /*CMP AL, BL*/
                addbyte(0xd8);
                if (not)
                        addbyte(0x75); /*JNZ +*/
                else
                        addbyte(0x74); /*JZ +*/
                break;
        }
        addbyte(7+5+(timing_bt ? 4 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(pc));
        addlong(op_pc+pc_offset+offset);
        if (timing_bt)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x6d);
                addbyte((uint8_t)cpu_state_offset(_cycles));
                addbyte(timing_bt);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}


static inline void FP_ENTER()
{
        if (codegen_fpu_entered)
                return;

        addbyte(0xf6); /*TEST cr0, 0xc*/
        addbyte(0x05);
        addlong((uintptr_t)&cr0);
        addbyte(0xc);
        addbyte(0x74); /*JZ +*/
        addbyte(7+7+5+5);
        addbyte(0xC7); /*MOVL [oldpc],op_old_pc*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(oldpc));
        addlong(op_old_pc);
        addbyte(0xc7); /*MOV [ESP], 7*/
        addbyte(0x04);
        addbyte(0x24);
        addlong(7);
        addbyte(0xe8); /*CALL x86_int*/
        addlong((uint32_t)x86_int - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));

        codegen_fpu_entered = 1;
}

static inline void FP_FLD(int reg)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xf3); /*MOVQ XMM0, ST[reg][EBP]*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + reg) & 7]));
                addbyte(0xc6); /*MOVB TOP[EBP], (TOP-1) & 7*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte((cpu_state.TOP - 1) & 7);
                addbyte(0xf3); /*MOVQ XMM1, MM[reg][EBP]*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0x4d);
                addbyte((uint8_t)cpu_state_offset(MM[(cpu_state.TOP + reg) & 7].q));
                addbyte(0x66); /*MOVQ ST[-1][EBP], XMM0*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP - 1) & 7]));
                addbyte(0x8a); /*MOV AL, tag[reg][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP + reg) & 7]));
                addbyte(0x66); /*MOVQ MM[-1][EBP], XMM1*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0x4d);
                addbyte((uint8_t)cpu_state_offset(MM[(cpu_state.TOP - 1) & 7].q));
                addbyte(0x88); /*MOV tag[-1][EBP], AL*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP - 1) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EAX, [TOP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV EBX, EAX*/
                addbyte(0xc3);
                if (reg)
                {
                        addbyte(0x83); /*ADD EAX, reg*/
                        addbyte(0xc0);
                        addbyte(reg);
                        addbyte(0x83); /*SUB EBX, 1*/
                        addbyte(0xeb);
                        addbyte(0x01);
                        addbyte(0x83); /*AND EAX, 7*/
                        addbyte(0xe0);
                        addbyte(0x07);
                }
                else
                {
                        addbyte(0x83); /*SUB EBX, 1*/
                        addbyte(0xeb);
                        addbyte(0x01);
                }

                addbyte(0xdd); /*FLD [ST+EAX*8]*/
                addbyte(0x44);
                addbyte(0xc5);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(0x07);
                addbyte(0x8b); /*MOV EDX, [ST_i64+EAX]*/
                addbyte(0x54);
                addbyte(0xc5);
                addbyte((uint8_t)cpu_state_offset(MM));
                addbyte(0x8b); /*MOV ECX, [ST_i64+4+EAX]*/
                addbyte(0x4c);
                addbyte(0xc5);
                addbyte((uint8_t)cpu_state_offset(MM)+4);
                addbyte(0x8a); /*MOV AL, [tag+EAX]*/
                addbyte(0x44);
                addbyte(0x05);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x88); /*MOV [tag+EBX], AL*/
                addbyte(0x44);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(0x89); /*MOV [ST_i64+EBX], EDX*/
                addbyte(0x54);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(MM));
                addbyte(0x89); /*MOV [ST_i64+EBX+4], ECX*/
                addbyte(0x4c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(MM)+4);

                addbyte(0x89); /*MOV [TOP], EBX*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
        }
}

static inline void FP_FST(int reg)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xf3); /*MOVQ XMM0, ST[0][EBP]*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x8a); /*MOV AL, tag[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(0x66); /*MOVQ ST[reg][EBP], XMM0*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + reg) & 7]));
                addbyte(0x88); /*MOV tag[reg][EBP], AL*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP + reg) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EAX, [TOP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0xdd); /*FLD [ST+EAX*8]*/
                addbyte(0x44);
                addbyte(0xc5);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x8a); /*MOV BL, [tag+EAX]*/
                addbyte(0x5c);
                addbyte(0x05);
                addbyte((uint8_t)cpu_state_offset(tag[0]));

                if (reg)
                {
                        addbyte(0x83); /*ADD EAX, reg*/
                        addbyte(0xc0);
                        addbyte(reg);
                        addbyte(0x83); /*AND EAX, 7*/
                        addbyte(0xe0);
                        addbyte(0x07);
                }

                addbyte(0xdd); /*FSTP [ST+EAX*8]*/
                addbyte(0x5c);
                addbyte(0xc5);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x88); /*MOV [tag+EAX], BL*/
                addbyte(0x5c);
                addbyte(0x05);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
        }
}

static inline void FP_FXCH(int reg)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xf3); /*MOVQ XMM0, ST[0][EBP]*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0xf3); /*MOVQ XMM1, ST[reg][EBP]*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0x4d);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + reg) & 7]));
                addbyte(0x66); /*MOVQ ST[reg][EBP], XMM0*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + reg) & 7]));
                addbyte(0xf3); /*MOVQ XMM2, MM[0][EBP]*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0x55);
                addbyte((uint8_t)cpu_state_offset(MM[cpu_state.TOP].q));
                addbyte(0x66); /*MOVQ ST[0][EBP], XMM1*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0x4d);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0xf3); /*MOVQ XMM3, MM[reg][EBP]*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(MM[(cpu_state.TOP + reg) & 7].q));
                addbyte(0x66); /*MOVQ MM[reg][EBP], XMM2*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0x55);
                addbyte((uint8_t)cpu_state_offset(MM[(cpu_state.TOP + reg) & 7].q));
                addbyte(0x8a); /*MOV AL, tag[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(0x66); /*MOVQ MM[0][EBP], XMM3*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(MM[cpu_state.TOP].q));
                addbyte(0x8a); /*MOV AH, tag[reg][EBP]*/
                addbyte(0x65);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP + reg) & 7]));
                addbyte(0x88); /*MOV tag[reg][EBP], AL*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP + reg) & 7]));
                addbyte(0x88); /*MOV tag[0][EBP], AH*/
                addbyte(0x65);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
        }
        else
        {
                addbyte(0x8b); /*MOV EAX, [TOP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV EBX, EAX*/
                addbyte(0xc3);
                addbyte(0x83); /*ADD EAX, reg*/
                addbyte(0xc0);
                addbyte(reg);

                addbyte(0xdd); /*FLD [ST+EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x83); /*AND EAX, 7*/
                addbyte(0xe0);
                addbyte(0x07);
                addbyte(0xdd); /*FLD [ST+EAX*8]*/
                addbyte(0x44);
                addbyte(0xc5);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0xdd); /*FSTP [ST+EAX*8]*/
                addbyte(0x5c);
                addbyte(0xc5);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x8a); /*MOV CL, tag[EAX]*/
                addbyte(0x4c);
                addbyte(0x05);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(0x8a); /*MOV DL, tag[EBX]*/
                addbyte(0x54);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(0x88); /*MOV tag[EBX], CL*/
                addbyte(0x4c);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(0x88); /*MOV tag[EAX], DL*/
                addbyte(0x54);
                addbyte(0x05);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(0xbe); /*MOVL ESI, ST_int64*/
                addlong((uintptr_t)cpu_state.MM);
                addbyte(0x8b); /*MOV ECX, ST_int64[EAX*8]*/
                addbyte(0x0c);
                addbyte(0xc6);
                addbyte(0x8b); /*MOV EDX, ST_int64[EBX*8]*/
                addbyte(0x14);
                addbyte(0xde);
                addbyte(0x89); /*MOV ST_int64[EBX*8], ECX*/
                addbyte(0x0c);
                addbyte(0xde);
                addbyte(0x89); /*MOV ST_int64[EAX*8], EDX*/
                addbyte(0x14);
                addbyte(0xc6);
                addbyte(0x8b); /*MOV ECX, ST_int64[EAX*8]+4*/
                addbyte(0x4c);
                addbyte(0xc6);
                addbyte(0x04);
                addbyte(0x8b); /*MOV EDX, ST_int64[EBX*8]+4*/
                addbyte(0x54);
                addbyte(0xde);
                addbyte(0x04);
                addbyte(0x89); /*MOV ST_int64[EBX*8]+4, ECX*/
                addbyte(0x4c);
                addbyte(0xde);
                addbyte(0x04);
                addbyte(0x89); /*MOV ST_int64[EAX*8]+4, EDX*/
                addbyte(0x54);
                addbyte(0xc6);
                addbyte(0x04);
        }
}


static inline void FP_LOAD_S()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x85); /*TEST EAX, EAX*/
                addbyte(0xc0);
                addbyte(0xd9); /*FLD [ESP]*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xc6); /*MOVB TOP[EBP], (TOP-1) & 7*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte((cpu_state.TOP - 1) & 7);
                addbyte(0x0f); /*SETE tag[reg][EBP]*/
                addbyte(0x94);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP - 1) & 7]));
                addbyte(0xdd); /*FSTP ST[reg][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP - 1) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x83); /*SUB EBX, 1*/
                addbyte(0xeb);
                addbyte(1);
                addbyte(0xd9); /*FLD [ESP]*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
                addbyte(0x85); /*TEST EAX, EAX*/
                addbyte(0xc0);
                addbyte(0x89); /*MOV TOP, EBX*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x0f); /*SETE [tag+EBX]*/
                addbyte(0x94);
                addbyte(0x44);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
        }
}
static inline void FP_LOAD_D()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV ST[reg][EBP], EAX*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP - 1) & 7]));
                addbyte(0x09); /*OR EAX, EDX*/
                addbyte(0xd0);
                addbyte(0x89); /*MOV ST[reg][EBP]+4, EDX*/
                addbyte(0x55);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP - 1) & 7]) + 4);
                addbyte(0xc6); /*MOVB TOP[EBP], (TOP-1) & 7*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte((cpu_state.TOP - 1) & 7);
                addbyte(0x0f); /*SETE tag[reg][EBP]*/
                addbyte(0x94);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP - 1) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x89); /*MOV [ESP+4], EDX*/
                addbyte(0x54);
                addbyte(0x24);
                addbyte(0x04);
                addbyte(0x83); /*SUB EBX, 1*/
                addbyte(0xeb);
                addbyte(1);
                addbyte(0x09); /*OR EAX, EDX*/
                addbyte(0xd0);
                addbyte(0xdd); /*FLD [ESP]*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
                addbyte(0x83); /*CMP EAX, 0*/
                addbyte(0xf8);
                addbyte(0);
                addbyte(0x89); /*MOV TOP, EBX*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x0f); /*SETE [tag+EBX]*/
                addbyte(0x94);
                addbyte(0x44);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
        }
}
static inline void FP_LOAD_IW()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x66); /*MOV [ESP], AX*/
                addbyte(0x89);
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x66); /*TEST AX, AX*/
                addbyte(0x85);
                addbyte(0xc0);
                addbyte(0xdf); /*FILDw [ESP]*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xc6); /*MOVB TOP[EBP], (TOP-1) & 7*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte((cpu_state.TOP - 1) & 7);
                addbyte(0x0f); /*SETE tag[reg][EBP]*/
                addbyte(0x94);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP - 1) & 7]));
                addbyte(0xdd); /*FSTP ST[reg][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP - 1) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x83); /*SUB EBX, 1*/
                addbyte(0xeb);
                addbyte(1);
                addbyte(0xdf); /*FILDw [ESP]*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
                addbyte(0x83); /*CMP EAX, 0*/
                addbyte(0xf8);
                addbyte(0);
                addbyte(0x89); /*MOV TOP, EBX*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x0f); /*SETE [tag+EBX]*/
                addbyte(0x94);
                addbyte(0x44);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
        }
}
static inline void FP_LOAD_IL()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x85); /*TEST EAX, EAX*/
                addbyte(0xc0);
                addbyte(0xdb); /*FILDl [ESP]*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xc6); /*MOVB TOP[EBP], (TOP-1) & 7*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte((cpu_state.TOP - 1) & 7);
                addbyte(0x0f); /*SETE tag[reg][EBP]*/
                addbyte(0x94);
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP - 1) & 7]));
                addbyte(0xdd); /*FSTP ST[reg][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP - 1) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x83); /*SUB EBX, 1*/
                addbyte(0xeb);
                addbyte(1);
                addbyte(0xdb); /*FILDl [ESP]*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
                addbyte(0x83); /*CMP EAX, 0*/
                addbyte(0xf8);
                addbyte(0);
                addbyte(0x89); /*MOV TOP, EBX*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x0f); /*SETE [tag+EBX]*/
                addbyte(0x94);
                addbyte(0x44);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
        }
}
static inline void FP_LOAD_IQ()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV MM[reg][EBP], EAX*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(MM[(cpu_state.TOP - 1) & 7].q));
                addbyte(0x09); /*OR EAX, EDX*/
                addbyte(0xd0);
                addbyte(0x89); /*MOV MM[reg][EBP]+4, EDX*/
                addbyte(0x55);
                addbyte((uint8_t)cpu_state_offset(MM[(cpu_state.TOP - 1) & 7].q) + 4);
                addbyte(0x0f); /*SETE AL*/
                addbyte(0x94);
                addbyte(0xc0);
                addbyte(0xdf); /*FILDq MM[reg][EBP]*/
                addbyte(0x6d);
                addbyte((uint8_t)cpu_state_offset(MM[(cpu_state.TOP - 1) & 7].q));
                addbyte(0x0c); /*OR AL, TAG_UINT64*/
                addbyte(TAG_UINT64);
                addbyte(0xc6); /*MOVB TOP[EBP], (TOP-1) & 7*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte((cpu_state.TOP - 1) & 7);
                addbyte(0x88); /*MOV tag[reg][EBP], AL*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP - 1) & 7]));
                addbyte(0xdd); /*FSTP ST[reg][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP - 1) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x83); /*SUB EBX, 1*/
                addbyte(0xeb);
                addbyte(1);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
                addbyte(0x89); /*MOV [ST_i64+EBX*8], EAX*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(MM));
                addbyte(0x09); /*OR EAX, EDX*/
                addbyte(0xd0);
                addbyte(0x89); /*MOV [ST_i64+4+EBX*8], EDX*/
                addbyte(0x54);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(MM)+4);
                addbyte(0x83); /*CMP EAX, 0*/
                addbyte(0xf8);
                addbyte(0);
                addbyte(0xdf); /*FILDl [ST_i64+EBX*8]*/
                addbyte(0x6c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(MM));
                addbyte(0x0f); /*SETE AL*/
                addbyte(0x94);
                addbyte(0xc0);
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x0c); /*OR AL, TAG_UINT64*/
                addbyte(TAG_UINT64);
                addbyte(0x89); /*MOV TOP, EBX*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x88); /*MOV [tag+EBX], AL*/
                addbyte(0x44);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
        }
}

static inline void FP_LOAD_IMM_Q(uint64_t v)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xc7); /*MOV ST[reg][EBP], v*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP - 1) & 7]));
                addlong(v & 0xffffffff);
                addbyte(0xc7); /*MOV ST[reg][EBP]+4, v*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP - 1) & 7]) + 4);
                addlong(v  >> 32);
                addbyte(0xc6); /*MOVB TOP[EBP], (TOP-1) & 7*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte((cpu_state.TOP - 1) & 7);
                addbyte(0xc6); /*MOVB tag[reg][EBP], 1:0*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP - 1) & 7]));
                addbyte(v ? 0 : 1);
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x83); /*SUB EBX, 1*/
                addbyte(0xeb);
                addbyte(1);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
                addbyte(0xc7); /*MOV ST[EBP+EBX*8], v*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addlong(v & 0xffffffff);
                addbyte(0xc7); /*MOV ST[EBP+EBX*8]+4, v*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST) + 4);
                addlong(v >> 32);
                addbyte(0xc6); /*MOVB tag[reg][EBP], 1:0*/
                addbyte(0x44);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(v ? 0 : 1);
                addbyte(0x89); /*MOV TOP, EBX*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
        }
}

static inline int FP_LOAD_REG(int reg)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xdd); /*FLD ST[reg][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + reg) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                if (reg)
                {
                        addbyte(0x83); /*ADD EBX, reg*/
                        addbyte(0xc3);
                        addbyte(reg);
                        addbyte(0x83); /*AND EBX, 7*/
                        addbyte(0xe3);
                        addbyte(7);
                }
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
        }
        addbyte(0xd9); /*FSTP [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);
        addbyte(0x8b); /*MOV EAX, [ESP]*/
        addbyte(0x04 | (REG_EBX << 3));
        addbyte(0x24);

        return REG_EBX;
}

static inline void FP_LOAD_REG_D(int reg, int *host_reg1, int *host_reg2)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xdd); /*FLD ST[reg][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + reg) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                if (reg)
                {
                        addbyte(0x83); /*ADD EBX, reg*/
                        addbyte(0xc3);
                        addbyte(reg);
                        addbyte(0x83); /*AND EBX, 7*/
                        addbyte(0xe3);
                        addbyte(7);
                }
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
        }
        addbyte(0xdd); /*FSTP [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);
        addbyte(0x8b); /*MOV EBX, [ESP]*/
        addbyte(0x04 | (REG_EBX << 3));
        addbyte(0x24);
        addbyte(0x8b); /*MOV ECX, [ESP+4]*/
        addbyte(0x44 | (REG_ECX << 3));
        addbyte(0x24);
        addbyte(0x04);

        *host_reg1 = REG_EBX;
        *host_reg2 = REG_ECX;
}

static inline int FP_LOAD_REG_INT_W(int reg)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x5d);
        addbyte((uint8_t)cpu_state_offset(TOP));
        if (reg)
        {
                addbyte(0x83); /*ADD EBX, reg*/
                addbyte(0xc3);
                addbyte(reg);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
        }
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x44);
        addbyte(0xdd);
        addbyte((uint8_t)cpu_state_offset(ST));

        addbyte(0xd9); /*FLDCW cpu_state.new_npxc*/
        addbyte(0x6d);
        addbyte((uint8_t)cpu_state_offset(new_npxc));
        addbyte(0xdb); /*FISTP [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);
        addbyte(0xd9); /*FLDCW cpu_state.old_npxc*/
        addbyte(0x6d);
        addbyte((uint8_t)cpu_state_offset(old_npxc));
        addbyte(0x8b); /*MOV EBX, [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);

        return REG_EBX;
}
static inline int FP_LOAD_REG_INT(int reg)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x5d);
        addbyte((uint8_t)cpu_state_offset(TOP));
        if (reg)
        {
                addbyte(0x83); /*ADD EBX, reg*/
                addbyte(0xc3);
                addbyte(reg);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
        }
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x44);
        addbyte(0xdd);
        addbyte((uint8_t)cpu_state_offset(ST));

        addbyte(0xd9); /*FLDCW cpu_state.new_npxc*/
        addbyte(0x6d);
        addbyte((uint8_t)cpu_state_offset(new_npxc));
        addbyte(0xdb); /*FISTP [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);
        addbyte(0xd9); /*FLDCW cpu_state.old_npxc*/
        addbyte(0x6d);
        addbyte((uint8_t)cpu_state_offset(old_npxc));
        addbyte(0x8b); /*MOV EBX, [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);

        return REG_EBX;
}
static inline void FP_LOAD_REG_INT_Q(int reg, int *host_reg1, int *host_reg2)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x5d);
        addbyte((uint8_t)cpu_state_offset(TOP));
        if (reg)
        {
                addbyte(0x83); /*ADD EBX, reg*/
                addbyte(0xc3);
                addbyte(reg);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
        }
        if (codegen_fpu_loaded_iq[cpu_state.TOP] && (cpu_state.tag[cpu_state.TOP] & TAG_UINT64))
        {
                /*If we know the register was loaded with FILDq in this block and
                  has not been modified, then we can skip most of the conversion
                  and just load the 64-bit integer representation directly */
                addbyte(0x8b); /*MOV ECX, [ST_i64+EBX*8]*/
                addbyte(0x4c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(MM)+4);
                addbyte(0x8b); /*MOV EBX, [ST_i64+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(MM));

                return;
        }

        addbyte(0xf6); /*TEST TAG[EBX], TAG_UINT64*/
        addbyte(0x44);
        addbyte(0x1d);
        addbyte((uint8_t)cpu_state_offset(tag[0]));
        addbyte(TAG_UINT64);
        addbyte(0x74); /*JZ +*/
        addbyte(4+4+2);

        addbyte(0x8b); /*MOV ECX, [ST_i64+EBX*8]*/
        addbyte(0x4c);
        addbyte(0xdd);
        addbyte((uint8_t)cpu_state_offset(MM)+4);
        addbyte(0x8b); /*MOV EBX, [ST_i64+EBX*8]*/
        addbyte(0x5c);
        addbyte(0xdd);
        addbyte((uint8_t)cpu_state_offset(MM));

        addbyte(0xeb); /*JMP done*/
        addbyte(4+3+3+3+3+4);

        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x44);
        addbyte(0xdd);
        addbyte((uint8_t)cpu_state_offset(ST));

        addbyte(0xd9); /*FLDCW cpu_state.new_npxc*/
        addbyte(0x6d);
        addbyte((uint8_t)cpu_state_offset(new_npxc));
        addbyte(0xdf); /*FISTPQ [ESP]*/
        addbyte(0x3c);
        addbyte(0x24);
        addbyte(0xd9); /*FLDCW cpu_state.old_npxc*/
        addbyte(0x6d);
        addbyte((uint8_t)cpu_state_offset(old_npxc));
        addbyte(0x8b); /*MOV EBX, [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);
        addbyte(0x8b); /*MOV ECX, 4[ESP]*/
        addbyte(0x4c);
        addbyte(0x24);
        addbyte(4);

        *host_reg1 = REG_EBX;
        *host_reg2 = REG_ECX;
}

static inline void FP_POP()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xc6); /*MOVB tag[0][EBP], 3*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(3);
                addbyte(0xc6); /*MOVB TOP[EBP], (TOP-1) & 7*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte((cpu_state.TOP + 1) & 7);
        }
        else
        {
                addbyte(0x8b); /*MOV EAX, TOP*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0xc6); /*MOVB tag[EAX], 3*/
                addbyte(0x44);
                addbyte(0x05);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(3);
                addbyte(0x04); /*ADD AL, 1*/
                addbyte(1);
                addbyte(0x24); /*AND AL, 7*/
                addbyte(7);
                addbyte(0x88); /*MOV TOP, AL*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
        }
}
static inline void FP_POP2()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xc6); /*MOVB tag[0][EBP], 3*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(3);
                addbyte(0xc6); /*MOVB tag[1][EBP], 3*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP+1)&7]));
                addbyte(3);
                addbyte(0xc6); /*MOVB TOP[EBP], (TOP+2) & 7*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte((cpu_state.TOP + 2) & 7);
        }
        else
        {
                addbyte(0x8b); /*MOV EAX, TOP*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0xc6); /*MOVB tag[EAX], 3*/
                addbyte(0x44);
                addbyte(0x05);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(3);
                addbyte(0x04); /*ADD AL, 2*/
                addbyte(2);
                addbyte(0x24); /*AND AL, 7*/
                addbyte(7);
                addbyte(0x88); /*MOV TOP, AL*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
        }
}

#define FPU_ADD  0x00
#define FPU_DIV  0x30
#define FPU_DIVR 0x38
#define FPU_MUL  0x08
#define FPU_SUB  0x20
#define FPU_SUBR 0x28

static inline void FP_OP_S(int op)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[dst][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x80); /*AND tag[dst][EBP], ~TAG_UINT64*/
                addbyte(0x65);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xd8); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP ST[dst][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
                addbyte(0x64);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xd8); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
        }
}
static inline void FP_OP_D(int op)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x89); /*MOV [ESP+4], EDX*/
                addbyte(0x54);
                addbyte(0x24);
                addbyte(0x04);
                if (((cpu_state.npxc >> 10) & 3) && op == FPU_ADD)
                {
                        addbyte(0xd9); /*FLDCW cpu_state.new_npxc*/
                        addbyte(0x6d);
                        addbyte((uint8_t)cpu_state_offset(new_npxc));
                }
                addbyte(0xdd); /*FLD ST[dst][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x80); /*AND tag[dst][EBP], ~TAG_UINT64*/
                addbyte(0x65);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xdc); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP ST[dst][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                if (((cpu_state.npxc >> 10) & 3) && op == FPU_ADD)
                {
                        addbyte(0xd9); /*FLDCW cpu_state.old_npxc*/
                        addbyte(0x6d);
                        addbyte((uint8_t)cpu_state_offset(old_npxc));
                }
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                if (((cpu_state.npxc >> 10) & 3) && op == FPU_ADD)
                {
                        addbyte(0xd9); /*FLDCW cpu_state.new_npxc*/
                        addbyte(0x6d);
                        addbyte((uint8_t)cpu_state_offset(new_npxc));
                }
                addbyte(0x89); /*MOV [ESP+4], EDX*/
                addbyte(0x54);
                addbyte(0x24);
                addbyte(0x04);
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
                addbyte(0x64);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xdc); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                if (((cpu_state.npxc >> 10) & 3) && op == FPU_ADD)
                {
                        addbyte(0xd9); /*FLDCW cpu_state.old_npxc*/
                        addbyte(0x6d);
                        addbyte((uint8_t)cpu_state_offset(old_npxc));
                }
        }
}
static inline void FP_OP_IW(int op)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x66); /*MOV [ESP], AX*/
                addbyte(0x89);
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x80); /*AND tag[0][EBP], ~TAG_UINT64*/
                addbyte(0x65);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xde); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP ST[0][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
                addbyte(0x64);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xde); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
        }
}
static inline void FP_OP_IL(int op)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x80); /*AND tag[0][EBP], ~TAG_UINT64*/
                addbyte(0x65);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xda); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP ST[0][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
                addbyte(0x64);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xda); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
        }
}
#if 0
static inline void FP_OP_IQ(int op)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x89); /*MOV [ESP+4], EDX*/
                addbyte(0x54);
                addbyte(0x24);
                addbyte(0x04);
                addbyte(0xdd); /*FLD ST[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x80); /*AND tag[0][EBP], ~TAG_UINT64*/
                addbyte(0x65);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xdc); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP ST[0][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x89); /*MOV [ESP+4], EDX*/
                addbyte(0x54);
                addbyte(0x24);
                addbyte(0x04);
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
                addbyte(0x64);
                addbyte(0x1d);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xdc); /*FADD [ESP]*/
                addbyte(0x04 | op);
                addbyte(0x24);
                addbyte(0xdd); /*FSTP [ST+EBX*8]*/
                addbyte(0x5c);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
        }
}
#endif

static inline void FP_COMPARE_S()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x8a); /*MOV BL, [npxs+1]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0x80); /*AND BL, ~(C0|C2|C3)*/
                addbyte(0xe3);
                addbyte((~(C0|C2|C3)) >> 8);
                addbyte(0xd8); /*FCOMP [ESP]*/
                addbyte(0x04 | 0x18);
                addbyte(0x24);
                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR BL, AH*/
                addbyte(0xe3);
                addbyte(0x88); /*MOV [npxs+1], BL*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x8a); /*MOV BL, [npxs+1]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0x80); /*AND BL, ~(C0|C2|C3)*/
                addbyte(0xe3);
                addbyte((~(C0|C2|C3)) >> 8);
                addbyte(0xd8); /*FCOMP [ESP]*/
                addbyte(0x04 | 0x18);
                addbyte(0x24);
                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR BL, AH*/
                addbyte(0xe3);
                addbyte(0x88); /*MOV [npxs+1], BL*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
}
static inline void FP_COMPARE_D()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x89); /*MOV [ESP+4], EDX*/
                addbyte(0x54);
                addbyte(0x24);
                addbyte(0x04);
                addbyte(0xdd); /*FLD ST[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x8a); /*MOV BL, [npxs+1]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0x80); /*AND BL, ~(C0|C2|C3)*/
                addbyte(0xe3);
                addbyte((~(C0|C2|C3)) >> 8);
                addbyte(0xdc); /*FCOMP [ESP]*/
                addbyte(0x04 | 0x18);
                addbyte(0x24);
                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR BL, AH*/
                addbyte(0xe3);
                addbyte(0x88); /*MOV [npxs+1], BL*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0x89); /*MOV [ESP+4], EDX*/
                addbyte(0x54);
                addbyte(0x24);
                addbyte(0x04);
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x8a); /*MOV BL, [npxs+1]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0x80); /*AND BL, ~(C0|C2|C3)*/
                addbyte(0xe3);
                addbyte((~(C0|C2|C3)) >> 8);
                addbyte(0xdc); /*FCOMP [ESP]*/
                addbyte(0x04 | 0x18);
                addbyte(0x24);
                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR BL, AH*/
                addbyte(0xe3);
                addbyte(0x88); /*MOV [npxs+1], BL*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
}
static inline void FP_COMPARE_IW()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x66); /*MOV [ESP], AX*/
                addbyte(0x89);
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x8a); /*MOV BL, [npxs+1]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0x80); /*AND BL, ~(C0|C2|C3)*/
                addbyte(0xe3);
                addbyte((~(C0|C2|C3)) >> 8);
                addbyte(0xde); /*FCOMP [ESP]*/
                addbyte(0x04 | 0x18);
                addbyte(0x24);
                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR BL, AH*/
                addbyte(0xe3);
                addbyte(0x88); /*MOV [npxs+1], BL*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x8a); /*MOV BL, [npxs+1]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0x80); /*AND BL, ~(C0|C2|C3)*/
                addbyte(0xe3);
                addbyte((~(C0|C2|C3)) >> 8);
                addbyte(0xde); /*FCOMP [ESP]*/
                addbyte(0x04 | 0x18);
                addbyte(0x24);
                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR BL, AH*/
                addbyte(0xe3);
                addbyte(0x88); /*MOV [npxs+1], BL*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
}
static inline void FP_COMPARE_IL()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0x8a); /*MOV BL, [npxs+1]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0x80); /*AND BL, ~(C0|C2|C3)*/
                addbyte(0xe3);
                addbyte((~(C0|C2|C3)) >> 8);
                addbyte(0xda); /*FCOMP [ESP]*/
                addbyte(0x04 | 0x18);
                addbyte(0x24);
                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR BL, AH*/
                addbyte(0xe3);
                addbyte(0x88); /*MOV [npxs+1], BL*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
        else
        {
                addbyte(0x8b); /*MOV EBX, TOP*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV [ESP], EAX*/
                addbyte(0x04);
                addbyte(0x24);
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x44);
                addbyte(0xdd);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x8a); /*MOV BL, [npxs+1]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0x80); /*AND BL, ~(C0|C2|C3)*/
                addbyte(0xe3);
                addbyte((~(C0|C2|C3)) >> 8);
                addbyte(0xda); /*FCOMP [ESP]*/
                addbyte(0x04 | 0x18);
                addbyte(0x24);
                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR BL, AH*/
                addbyte(0xe3);
                addbyte(0x88); /*MOV [npxs+1], BL*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
}

static inline void FP_OP_REG(int op, int dst, int src)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xdd); /*FLD ST[dst][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + dst) & 7]));
                addbyte(0xdc); /*FADD ST[src][EBP]*/
                addbyte(0x45 | op);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + src) & 7]));
                addbyte(0x80); /*AND tag[dst][EBP], ~TAG_UINT64*/
                addbyte(0x65);
                addbyte((uint8_t)cpu_state_offset(tag[(cpu_state.TOP + dst) & 7]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xdd); /*FSTP ST[dst][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + dst) & 7]));
        }
        else
        {
                addbyte(0x8b); /*MOV EAX, TOP*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV EBX, EAX*/
                addbyte(0xc3);
                if (src || dst)
                {
                        addbyte(0x83); /*ADD EAX, 1*/
                        addbyte(0xc0);
                        addbyte(src ? src : dst);
                        addbyte(0x83); /*AND EAX, 7*/
                        addbyte(0xe0);
                        addbyte(7);
                }

                if (src)
                {
                        addbyte(0xdd); /*FLD ST[EBX*8]*/
                        addbyte(0x44);
                        addbyte(0xdd);
                        addbyte((uint8_t)cpu_state_offset(ST));
                        addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
                        addbyte(0x64);
                        addbyte(0x1d);
                        addbyte((uint8_t)cpu_state_offset(tag[0]));
                        addbyte(TAG_NOT_UINT64);
                        addbyte(0xdc); /*FADD ST[EAX*8]*/
                        addbyte(0x44 | op);
                        addbyte(0xc5);
                        addbyte((uint8_t)cpu_state_offset(ST));
                        addbyte(0xdd); /*FSTP ST[EBX*8]*/
                        addbyte(0x5c);
                        addbyte(0xdd);
                        addbyte((uint8_t)cpu_state_offset(ST));
                }
                else
                {
                        addbyte(0xdd); /*FLD [ESI+EAX*8]*/
                        addbyte(0x44);
                        addbyte(0xc5);
                        addbyte((uint8_t)cpu_state_offset(ST));
                        addbyte(0x80); /*AND tag[EAX], ~TAG_UINT64*/
                        addbyte(0x64);
                        addbyte(0x05);
                        addbyte((uint8_t)cpu_state_offset(tag[0]));
                        addbyte(TAG_NOT_UINT64);
                        addbyte(0xdc); /*FADD ST[EBX*8]*/
                        addbyte(0x44 | op);
                        addbyte(0xdd);
                        addbyte((uint8_t)cpu_state_offset(ST));
                        addbyte(0xdd); /*FSTP ST[EAX*8]*/
                        addbyte(0x5c);
                        addbyte(0xc5);
                        addbyte((uint8_t)cpu_state_offset(ST));
                }
        }
}

static inline void FP_COMPARE_REG(int dst, int src)
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0x8a); /*MOV CL, [npxs+1]*/
                addbyte(0x4d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0xdd); /*FLD ST[dst][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + dst) & 7]));
                addbyte(0x80); /*AND CL, ~(C0|C2|C3)*/
                addbyte(0xe1);
                addbyte((~(C0|C2|C3)) >> 8);
                addbyte(0xdc); /*FCOMP ST[src][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[(cpu_state.TOP + src) & 7]));
                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR CL, AH*/
                addbyte(0xe1);
                addbyte(0x88); /*MOV [npxs+1], CL*/
                addbyte(0x4d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
        else
        {
                addbyte(0x8b); /*MOV EAX, TOP*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));
                addbyte(0x89); /*MOV EBX, EAX*/
                addbyte(0xc3);
                if (src || dst)
                {
                        addbyte(0x83); /*ADD EAX, 1*/
                        addbyte(0xc0);
                        addbyte(src ? src : dst);
                        addbyte(0x83); /*AND EAX, 7*/
                        addbyte(0xe0);
                        addbyte(7);
                }

                addbyte(0x8a); /*MOV CL, [npxs+1]*/
                addbyte(0x4d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
                addbyte(0xdb); /*FCLEX*/
                addbyte(0xe2);
                addbyte(0x80); /*AND CL, ~(C0|C2|C3)*/
                addbyte(0xe1);
                addbyte((~(C0|C2|C3)) >> 8);

                if (src)
                {
                        addbyte(0xdd); /*FLD ST[EBX*8]*/
                        addbyte(0x44);
                        addbyte(0xdd);
                        addbyte((uint8_t)cpu_state_offset(ST));
                        addbyte(0xdc); /*FCOMP ST[EAX*8]*/
                        addbyte(0x44 | 0x18);
                        addbyte(0xc5);
                        addbyte((uint8_t)cpu_state_offset(ST));
                }
                else
                {
                        addbyte(0xdd); /*FLD [ESI+EAX*8]*/
                        addbyte(0x44);
                        addbyte(0xc5);
                        addbyte((uint8_t)cpu_state_offset(ST));
                        addbyte(0xdc); /*FCOMP ST[EBX*8]*/
                        addbyte(0x44 | 0x18);
                        addbyte(0xdd);
                        addbyte((uint8_t)cpu_state_offset(ST));
                }

                addbyte(0xdf); /*FSTSW AX*/
                addbyte(0xe0);
                addbyte(0x80); /*AND AH, (C0|C2|C3)*/
                addbyte(0xe4);
                addbyte((C0|C2|C3) >> 8);
                addbyte(0x08); /*OR CL, AH*/
                addbyte(0xe1);
                addbyte(0x88); /*MOV [npxs+1], CL*/
                addbyte(0x4d);
                addbyte((uint8_t)cpu_state_offset(npxs) + 1);
        }
}

static inline void FP_FCHS()
{
        if (codeblock[block_current].flags & CODEBLOCK_STATIC_TOP)
        {
                addbyte(0xdd); /*FLD ST[0][EBP]*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
                addbyte(0xd9); /*FCHS*/
                addbyte(0xe0);
                addbyte(0x80); /*AND tag[dst][EBP], ~TAG_UINT64*/
                addbyte(0x65);
                addbyte((uint8_t)cpu_state_offset(tag[cpu_state.TOP]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xdd); /*FSTP ST[dst][EBP]*/
                addbyte(0x5d);
                addbyte((uint8_t)cpu_state_offset(ST[cpu_state.TOP]));
        }
        else
        {
                addbyte(0x8b); /*MOV EAX, TOP*/
                addbyte(0x45);
                addbyte((uint8_t)cpu_state_offset(TOP));

                addbyte(0xdd); /*FLD [ESI+EAX*8]*/
                addbyte(0x44);
                addbyte(0xc5);
                addbyte((uint8_t)cpu_state_offset(ST));
                addbyte(0x80); /*AND tag[EAX], ~TAG_UINT64*/
                addbyte(0x64);
                addbyte(0x05);
                addbyte((uint8_t)cpu_state_offset(tag[0]));
                addbyte(TAG_NOT_UINT64);
                addbyte(0xd9); /*FCHS*/
                addbyte(0xe0);
                addbyte(0xdd); /*FSTP ST[EAX*8]*/
                addbyte(0x5c);
                addbyte(0xc5);
                addbyte((uint8_t)cpu_state_offset(ST));
        }
}

static inline void UPDATE_NPXC(int reg)
{
        addbyte(0x66); /*AND cpu_state.new_npxc, ~0xc00*/
        addbyte(0x81);
        addbyte(0x65);
        addbyte((uint8_t)cpu_state_offset(new_npxc));
        addword(~0xc00);
        if (reg)
        {
                addbyte(0x66); /*AND reg, 0xc00*/
                addbyte(0x81);
                addbyte(0xe0 | reg);
                addword(0xc00);
        }
        else
        {
                addbyte(0x66); /*AND AX, 0xc00*/
                addbyte(0x25);
                addword(0xc00);
        }
        addbyte(0x66); /*OR cpu_state.new_npxc, reg*/
        addbyte(0x09);
        addbyte(0x45 | (reg << 3));
        addbyte((uint8_t)cpu_state_offset(new_npxc));
}

static inline int ZERO_EXTEND_W_B(int reg)
{
        addbyte(0x0f); /*MOVZX regl, regb*/
        addbyte(0xb6);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}
static inline int ZERO_EXTEND_L_B(int reg)
{
        addbyte(0x0f); /*MOVZX regl, regb*/
        addbyte(0xb6);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}
static inline int ZERO_EXTEND_L_W(int reg)
{
        addbyte(0x0f); /*MOVZX regl, regw*/
        addbyte(0xb7);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}

static inline int SIGN_EXTEND_W_B(int reg)
{
        addbyte(0x0f); /*MOVSX regl, regb*/
        addbyte(0xbe);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}
static inline int SIGN_EXTEND_L_B(int reg)
{
        addbyte(0x0f); /*MOVSX regl, regb*/
        addbyte(0xbe);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}
static inline int SIGN_EXTEND_L_W(int reg)
{
        addbyte(0x0f); /*MOVSX regl, regw*/
        addbyte(0xbf);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}

static inline int COPY_REG(int src_reg)
{
        return src_reg;
}

static inline void SET_BITS(uintptr_t addr, uint32_t val)
{
        if (val & ~0xff)
        {
                addbyte(0x81);
                addbyte(0x0d);
                addlong(addr);
                addlong(val);
        }
        else
       {
                addbyte(0x80);
                addbyte(0x0d);
                addlong(addr);
                addbyte(val);
        }
}
static inline void CLEAR_BITS(uintptr_t addr, uint32_t val)
{
        if (val & ~0xff)
        {
                addbyte(0x81);
                addbyte(0x25);
                addlong(addr);
                addlong(~val);
        }
        else
       {
                addbyte(0x80);
                addbyte(0x25);
                addlong(addr);
                addbyte(~val);
        }
}

#define LOAD_Q_REG_1 REG_EAX
#define LOAD_Q_REG_2 REG_EDX

static inline void MMX_ENTER()
{
        if (codegen_mmx_entered)
                return;

        addbyte(0xf6); /*TEST cr0, 0xc*/
        addbyte(0x05);
        addlong((uintptr_t)&cr0);
        addbyte(0xc);
        addbyte(0x74); /*JZ +*/
        addbyte(7+7+5+5);
        addbyte(0xC7); /*MOVL [oldpc],op_old_pc*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(oldpc));
        addlong(op_old_pc);
        addbyte(0xc7); /*MOV [ESP], 7*/
        addbyte(0x04);
        addbyte(0x24);
        addlong(7);
        addbyte(0xe8); /*CALL x86_int*/
        addlong((uint32_t)x86_int - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));

        addbyte(0x31); /*XOR EAX, EAX*/
        addbyte(0xc0);
        addbyte(0xc6); /*MOV ISMMX, 1*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(ismmx));
        addbyte(1);
        addbyte(0x89); /*MOV TOP, EAX*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(TOP));
        addbyte(0x89); /*MOV tag, EAX*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(tag[0]));
        addbyte(0x89); /*MOV tag+4, EAX*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(tag[4]));

        codegen_mmx_entered = 1;
}

extern int mmx_ebx_ecx_loaded;

static inline int LOAD_MMX_D(int guest_reg)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = 100;

        addbyte(0x8b); /*MOV EBX, reg*/
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint8_t)cpu_state_offset(MM[guest_reg].l[0]));

        return host_reg;
}
static inline void LOAD_MMX_Q(int guest_reg, int *host_reg1, int *host_reg2)
{
        if (!mmx_ebx_ecx_loaded)
        {
                *host_reg1 = REG_EBX;
                *host_reg2 = REG_ECX;
                mmx_ebx_ecx_loaded = 1;
        }
        else
        {
                *host_reg1 = REG_EAX;
                *host_reg2 = REG_EDX;
        }

        addbyte(0x8b); /*MOV EBX, reg*/
        addbyte(0x45 | ((*host_reg1) << 3));
        addbyte((uint8_t)cpu_state_offset(MM[guest_reg].l[0]));
        addbyte(0x8b); /*MOV ECX, reg+4*/
        addbyte(0x45 | ((*host_reg2) << 3));
        addbyte((uint8_t)cpu_state_offset(MM[guest_reg].l[1]));
}
static inline int LOAD_MMX_Q_MMX(int guest_reg)
{
        int dst_reg = find_host_xmm_reg();
        host_reg_xmm_mapping[dst_reg] = guest_reg;

        addbyte(0xf3); /*MOVQ dst_reg,[reg]*/
        addbyte(0x0f);
        addbyte(0x7e);
        addbyte(0x45 | (dst_reg << 3));
        addbyte((uint8_t)cpu_state_offset(MM[guest_reg].q));

        return dst_reg;
}

static inline int LOAD_INT_TO_MMX(int src_reg1, int src_reg2)
{
        int dst_reg = find_host_xmm_reg();
        host_reg_xmm_mapping[dst_reg] = 100;

        addbyte(0x66); /*MOVD dst_reg, src_reg1*/
        addbyte(0x0f);
        addbyte(0x6e);
        addbyte(0xc0 | (dst_reg << 3) | src_reg1);
        addbyte(0x66); /*MOVD XMM7, src_reg2*/
        addbyte(0x0f);
        addbyte(0x6e);
        addbyte(0xc0 | (7 << 3) | src_reg2);
        addbyte(0x66); /*PUNPCKLDQ dst_reg, XMM7*/
        addbyte(0x0f);
        addbyte(0x62);
        addbyte(0xc0 | 7 | (dst_reg << 3));

        return dst_reg;
}

static inline void STORE_MMX_LQ(int guest_reg, int host_reg1)
{
        addbyte(0xC7); /*MOVL [reg],0*/
        addbyte(0x45);
        addbyte((uint8_t)cpu_state_offset(MM[guest_reg].l[1]));
        addlong(0);
        addbyte(0x89); /*MOVL [reg],host_reg*/
        addbyte(0x45 | (host_reg1 << 3));
        addbyte((uint8_t)cpu_state_offset(MM[guest_reg].l[0]));
}
static inline void STORE_MMX_Q(int guest_reg, int host_reg1, int host_reg2)
{
        addbyte(0x89); /*MOVL [reg],host_reg*/
        addbyte(0x45 | (host_reg1 << 3));
        addbyte((uint8_t)cpu_state_offset(MM[guest_reg].l[0]));
        addbyte(0x89); /*MOVL [reg],host_reg*/
        addbyte(0x45 | (host_reg2 << 3));
        addbyte((uint8_t)cpu_state_offset(MM[guest_reg].l[1]));
}
static inline void STORE_MMX_Q_MMX(int guest_reg, int host_reg)
{
        addbyte(0x66); /*MOVQ [guest_reg],host_reg*/
        addbyte(0x0f);
        addbyte(0xd6);
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint8_t)cpu_state_offset(MM[guest_reg].q));
}

#define MMX_x86_OP(name, opcode)                            \
static inline void MMX_ ## name(int dst_reg, int src_reg)      \
{                                                       \
        addbyte(0x66); /*op dst_reg, src_reg*/          \
        addbyte(0x0f);                                  \
        addbyte(opcode);                                \
        addbyte(0xc0 | (dst_reg << 3) | src_reg);       \
}

MMX_x86_OP(AND,  0xdb)
MMX_x86_OP(ANDN, 0xdf)
MMX_x86_OP(OR,   0xeb)
MMX_x86_OP(XOR,  0xef)

MMX_x86_OP(ADDB,   0xfc)
MMX_x86_OP(ADDW,   0xfd)
MMX_x86_OP(ADDD,   0xfe)
MMX_x86_OP(ADDSB,  0xec)
MMX_x86_OP(ADDSW,  0xed)
MMX_x86_OP(ADDUSB, 0xdc)
MMX_x86_OP(ADDUSW, 0xdd)

MMX_x86_OP(SUBB,   0xf8)
MMX_x86_OP(SUBW,   0xf9)
MMX_x86_OP(SUBD,   0xfa)
MMX_x86_OP(SUBSB,  0xe8)
MMX_x86_OP(SUBSW,  0xe9)
MMX_x86_OP(SUBUSB, 0xd8)
MMX_x86_OP(SUBUSW, 0xd9)

MMX_x86_OP(PUNPCKLBW, 0x60);
MMX_x86_OP(PUNPCKLWD, 0x61);
MMX_x86_OP(PUNPCKLDQ, 0x62);
MMX_x86_OP(PCMPGTB,   0x64);
MMX_x86_OP(PCMPGTW,   0x65);
MMX_x86_OP(PCMPGTD,   0x66);

MMX_x86_OP(PCMPEQB,   0x74);
MMX_x86_OP(PCMPEQW,   0x75);
MMX_x86_OP(PCMPEQD,   0x76);

MMX_x86_OP(PSRLW,   0xd1);
MMX_x86_OP(PSRLD,   0xd2);
MMX_x86_OP(PSRLQ,   0xd3);
MMX_x86_OP(PSRAW,   0xe1);
MMX_x86_OP(PSRAD,   0xe2);
MMX_x86_OP(PSLLW,   0xf1);
MMX_x86_OP(PSLLD,   0xf2);
MMX_x86_OP(PSLLQ,   0xf3);

MMX_x86_OP(PMULLW,  0xd5);
MMX_x86_OP(PMULHW,  0xe5);
MMX_x86_OP(PMADDWD, 0xf5);

static inline void MMX_PACKSSWB(int dst_reg, int src_reg)
{
        addbyte(0x66); /*PACKSSWB dst_reg, src_reg*/
        addbyte(0x0f);
        addbyte(0x63);
        addbyte(0xc0 | (dst_reg << 3) | src_reg);
        addbyte(0x66); /*PSHUFD dst_reg, dst_reg*/
        addbyte(0x0f);
        addbyte(0x70);
        addbyte(0xc0 | (dst_reg << 3) | dst_reg);
        addbyte(0x08);
}
static inline void MMX_PACKUSWB(int dst_reg, int src_reg)
{
        addbyte(0x66); /*PACKUSWB dst_reg, src_reg*/
        addbyte(0x0f);
        addbyte(0x67);
        addbyte(0xc0 | (dst_reg << 3) | src_reg);
        addbyte(0x66); /*PSHUFD dst_reg, dst_reg*/
        addbyte(0x0f);
        addbyte(0x70);
        addbyte(0xc0 | (dst_reg << 3) | dst_reg);
        addbyte(0x08);
}
static inline void MMX_PACKSSDW(int dst_reg, int src_reg)
{
        addbyte(0x66); /*PACKSSDW dst_reg, src_reg*/
        addbyte(0x0f);
        addbyte(0x6b);
        addbyte(0xc0 | (dst_reg << 3) | src_reg);
        addbyte(0x66); /*PSHUFD dst_reg, dst_reg*/
        addbyte(0x0f);
        addbyte(0x70);
        addbyte(0xc0 | (dst_reg << 3) | dst_reg);
        addbyte(0x08);
}
static inline void MMX_PUNPCKHBW(int dst_reg, int src_reg)
{
        addbyte(0x66); /*PUNPCKLBW dst_reg, src_reg*/
        addbyte(0x0f);
        addbyte(0x60);
        addbyte(0xc0 | (dst_reg << 3) | src_reg);
        addbyte(0x66); /*PSHUFD dst_reg, dst_reg*/
        addbyte(0x0f);
        addbyte(0x70);
        addbyte(0xc0 | (dst_reg << 3) | dst_reg);
        addbyte(0x0e);
}
static inline void MMX_PUNPCKHWD(int dst_reg, int src_reg)
{
        addbyte(0x66); /*PUNPCKLWD dst_reg, src_reg*/
        addbyte(0x0f);
        addbyte(0x61);
        addbyte(0xc0 | (dst_reg << 3) | src_reg);
        addbyte(0x66); /*PSHUFD dst_reg, dst_reg*/
        addbyte(0x0f);
        addbyte(0x70);
        addbyte(0xc0 | (dst_reg << 3) | dst_reg);
        addbyte(0x0e);
}
static inline void MMX_PUNPCKHDQ(int dst_reg, int src_reg)
{
        addbyte(0x66); /*PUNPCKLDQ dst_reg, src_reg*/
        addbyte(0x0f);
        addbyte(0x62);
        addbyte(0xc0 | (dst_reg << 3) | src_reg);
        addbyte(0x66); /*PSHUFD dst_reg, dst_reg*/
        addbyte(0x0f);
        addbyte(0x70);
        addbyte(0xc0 | (dst_reg << 3) | dst_reg);
        addbyte(0x0e);
}

static inline void MMX_PSRLW_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRLW dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x71);
        addbyte(0xc0 | dst_reg | 0x10);
        addbyte(amount);
}
static inline void MMX_PSRAW_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRAW dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x71);
        addbyte(0xc0 | dst_reg | 0x20);
        addbyte(amount);
}
static inline void MMX_PSLLW_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSLLW dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x71);
        addbyte(0xc0 | dst_reg | 0x30);
        addbyte(amount);
}

static inline void MMX_PSRLD_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRLD dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x72);
        addbyte(0xc0 | dst_reg | 0x10);
        addbyte(amount);
}
static inline void MMX_PSRAD_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRAD dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x72);
        addbyte(0xc0 | dst_reg | 0x20);
        addbyte(amount);
}
static inline void MMX_PSLLD_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSLLD dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x72);
        addbyte(0xc0 | dst_reg | 0x30);
        addbyte(amount);
}

static inline void MMX_PSRLQ_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRLQ dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x73);
        addbyte(0xc0 | dst_reg | 0x10);
        addbyte(amount);
}
static inline void MMX_PSRAQ_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRAQ dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x73);
        addbyte(0xc0 | dst_reg | 0x20);
        addbyte(amount);
}
static inline void MMX_PSLLQ_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSLLQ dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x73);
        addbyte(0xc0 | dst_reg | 0x30);
        addbyte(amount);
}


static inline void SAVE_EA()
{
        addbyte(0x89); /*MOV [ESP+12], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(12);
}
static inline void LOAD_EA()
{
        addbyte(0x8b); /*MOV EAX, [ESP+12]*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(12);
}

#define MEM_CHECK_WRITE_B MEM_CHECK_WRITE
static inline void MEM_CHECK_WRITE(x86seg *seg)
{
        CHECK_SEG_WRITE(seg);
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_check_write*/
        addlong(mem_check_write - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        LOAD_EA();
}
static inline void MEM_CHECK_WRITE_W(x86seg *seg)
{
        CHECK_SEG_WRITE(seg);
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_check_write_w*/
        addlong(mem_check_write_w - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        LOAD_EA();
}
static inline void MEM_CHECK_WRITE_L(x86seg *seg)
{
        CHECK_SEG_WRITE(seg);
        if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)))
        {
                addbyte(0x31); /*XOR ESI, ESI*/
                addbyte(0xf6);
        }
        else
        {
                addbyte(0x8b); /*MOVL ESI, seg->base*/
                addbyte(0x05 | (REG_ESI << 3));
                addlong((uint32_t)&seg->base);
        }
        addbyte(0xe8); /*CALL mem_check_write_l*/
        addlong(mem_check_write_l - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        LOAD_EA();
}

static inline void LOAD_SEG(int host_reg, void *seg)
{
        addbyte(0xc7); /*MOV [ESP+4], seg*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(4);
        addlong((uint32_t)seg);
        addbyte(0x89); /*MOV [ESP], host_reg*/
        addbyte(0x04 | (host_reg << 3));
        addbyte(0x24);
        CALL_FUNC((uintptr_t)loadseg);
        addbyte(0x80); /*CMP abrt, 0*/
        addbyte(0x7d);
        addbyte((uint8_t)cpu_state_offset(abrt));
        addbyte(0);
        addbyte(0x0f); /*JNE end*/
        addbyte(0x85);
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}
