/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
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

static void STORE_IMM_ADDR_B(uintptr_t addr, uint8_t val)
{
        addbyte(0xC6); /*MOVB [addr],val*/
        addbyte(0x05);
        addlong(addr);
        addbyte(val);
}
static void STORE_IMM_ADDR_W(uintptr_t addr, uint16_t val)
{
        addbyte(0x66); /*MOVW [addr],val*/
        addbyte(0xC7);
        addbyte(0x05);
        addlong(addr);
        addword(val);
}
static void STORE_IMM_ADDR_L(uintptr_t addr, uint32_t val)
{
        if (addr >= (uintptr_t)&cpu_state && addr < ((uintptr_t)&cpu_state)+0x80)
        {
                addbyte(0xC7); /*MOVL [addr],val*/
                addbyte(0x45);
                addbyte(addr - (uint32_t)&cpu_state);
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

static void STORE_IMM_REG_B(int reg, uint8_t val)
{
        addbyte(0xC6); /*MOVB [addr],val*/
        addbyte(0x45);
        if (reg & 4)
                addbyte((uint32_t)&cpu_state.regs[reg & 3].b.h - (uint32_t)&EAX);
        else
                addbyte((uint32_t)&cpu_state.regs[reg & 3].b.l - (uint32_t)&EAX);
        addbyte(val);
}
static void STORE_IMM_REG_W(int reg, uint16_t val)
{
        addbyte(0x66); /*MOVW [addr],val*/
        addbyte(0xC7);
        addbyte(0x45);
        addbyte((uint32_t)&cpu_state.regs[reg & 7].w - (uint32_t)&EAX);
        addword(val);
}
static void STORE_IMM_REG_L(int reg, uint32_t val)
{
        addbyte(0xC7); /*MOVL [addr],val*/
        addbyte(0x45);
        addbyte((uint32_t)&cpu_state.regs[reg & 7].l - (uint32_t)&EAX);
        addlong(val);
}

static int LOAD_REG_B(int reg)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = reg;

        addbyte(0x0f); /*MOVZX B[reg],host_reg*/
        addbyte(0xb6);
        addbyte(0x45 | (host_reg << 3));
        if (reg & 4)
                addbyte((uint32_t)&cpu_state.regs[reg & 3].b.h - (uint32_t)&EAX);
        else
                addbyte((uint32_t)&cpu_state.regs[reg & 3].b.l - (uint32_t)&EAX);

        return host_reg;
}
static int LOAD_REG_W(int reg)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = reg;

        addbyte(0x0f); /*MOVZX W[reg],host_reg*/
        addbyte(0xb7);
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint32_t)&cpu_state.regs[reg & 7].w - (uint32_t)&EAX);

        return host_reg;
}
static int LOAD_REG_L(int reg)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = reg;

        addbyte(0x8b); /*MOVL host_reg,[reg]*/
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint32_t)&cpu_state.regs[reg & 7].l - (uint32_t)&EAX);

        return host_reg;
}

static int LOAD_VAR_W(uintptr_t addr)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = 0;

        addbyte(0x66); /*MOVL host_reg,[reg]*/
        addbyte(0x8b);
        addbyte(0x05 | (host_reg << 3));
        addlong((uint32_t)addr);

        return host_reg;
}
static int LOAD_VAR_L(uintptr_t addr)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = 0;

        addbyte(0x8b); /*MOVL host_reg,[reg]*/
        addbyte(0x05 | (host_reg << 3));
        addlong((uint32_t)addr);

        return host_reg;
}

static int LOAD_REG_IMM(uint32_t imm)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = 0;
        
        addbyte(0xc7); /*MOVL host_reg, imm*/
        addbyte(0xc0 | host_reg);
        addlong(imm);
        
        return host_reg;
}

static int LOAD_HOST_REG(int host_reg)
{
        int new_host_reg = find_host_reg();
        host_reg_mapping[new_host_reg] = 0;
        
        addbyte(0x89); /*MOV new_host_reg, host_reg*/
        addbyte(0xc0 | (host_reg << 3) | new_host_reg);
        
        return new_host_reg;
}

static void STORE_REG_B_RELEASE(int host_reg)
{
        addbyte(0x88); /*MOVB [reg],host_reg*/
        addbyte(0x45 | (host_reg << 3));
        if (host_reg_mapping[host_reg] & 4)
                addbyte((uint32_t)&cpu_state.regs[host_reg_mapping[host_reg] & 3].b.h - (uint32_t)&EAX);
        else
                addbyte((uint32_t)&cpu_state.regs[host_reg_mapping[host_reg] & 3].b.l - (uint32_t)&EAX);
        host_reg_mapping[host_reg] = -1;
}
static void STORE_REG_W_RELEASE(int host_reg)
{
        addbyte(0x66); /*MOVW [reg],host_reg*/
        addbyte(0x89);
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint32_t)&cpu_state.regs[host_reg_mapping[host_reg]].w - (uint32_t)&EAX);
        host_reg_mapping[host_reg] = -1;
}
static void STORE_REG_L_RELEASE(int host_reg)
{
        addbyte(0x89); /*MOVL [reg],host_reg*/
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint32_t)&cpu_state.regs[host_reg_mapping[host_reg]].l - (uint32_t)&EAX);
        host_reg_mapping[host_reg] = -1;
}

static void STORE_REG_TARGET_B_RELEASE(int host_reg, int guest_reg)
{
        addbyte(0x88); /*MOVB [guest_reg],host_reg*/
        addbyte(0x45 | (host_reg << 3));
        if (guest_reg & 4)
                addbyte((uint32_t)&cpu_state.regs[guest_reg & 3].b.h - (uint32_t)&EAX);
        else
                addbyte((uint32_t)&cpu_state.regs[guest_reg & 3].b.l - (uint32_t)&EAX);
        host_reg_mapping[host_reg] = -1;
}
static void STORE_REG_TARGET_W_RELEASE(int host_reg, int guest_reg)
{
        addbyte(0x66); /*MOVW [guest_reg],host_reg*/
        addbyte(0x89);
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint32_t)&cpu_state.regs[guest_reg & 7].w - (uint32_t)&EAX);
        host_reg_mapping[host_reg] = -1;
}
static void STORE_REG_TARGET_L_RELEASE(int host_reg, int guest_reg)
{
        addbyte(0x89); /*MOVL [guest_reg],host_reg*/
        addbyte(0x45 | (host_reg << 3));
        addbyte((uint32_t)&cpu_state.regs[guest_reg & 7].l - (uint32_t)&EAX);
        host_reg_mapping[host_reg] = -1;
}

static void RELEASE_REG(int host_reg)
{
        host_reg_mapping[host_reg] = -1;
}

static void STORE_HOST_REG_ADDR_W(uintptr_t addr, int host_reg)
{
        if (addr >= (uintptr_t)&cpu_state && addr < ((uintptr_t)&cpu_state)+0x80)
        {
                addbyte(0x66); /*MOVW [addr],host_reg*/
                addbyte(0x89);
                addbyte(0x45 | (host_reg << 3));
                addbyte((uint32_t)addr - (uint32_t)&cpu_state);
        }
        else
        {
                addbyte(0x66); /*MOVL [reg],host_reg*/
                addbyte(0x89);
                addbyte(0x05 | (host_reg << 3));
                addlong(addr);
        }
}
static void STORE_HOST_REG_ADDR(uintptr_t addr, int host_reg)
{
        if (addr >= (uintptr_t)&cpu_state && addr < ((uintptr_t)&cpu_state)+0x80)
        {
                addbyte(0x89); /*MOVL [addr],host_reg*/
                addbyte(0x45 | (host_reg << 3));
                addbyte((uint32_t)addr - (uint32_t)&cpu_state);
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

static void ADD_HOST_REG_B(int dst_reg, int src_reg)
{
        addbyte(0x00); /*ADDB dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static void ADD_HOST_REG_W(int dst_reg, int src_reg)
{
        addbyte(0x66); /*ADDW dst_reg, src_reg*/
        addbyte(0x01);
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static void ADD_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x01); /*ADDL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static void ADD_HOST_REG_IMM_B(int host_reg, uint8_t imm)
{
        addbyte(0x80); /*ADDB host_reg, imm*/
        addbyte(0xC0 | host_reg);
        addbyte(imm);
}
static void ADD_HOST_REG_IMM_W(int host_reg, uint16_t imm)
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
static void ADD_HOST_REG_IMM(int host_reg, uint32_t imm)
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
static void AND_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x21); /*ANDL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static void AND_HOST_REG_IMM(int host_reg, uint32_t imm)
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
static int TEST_HOST_REG_B(int dst_reg, int src_reg)
{
        AND_HOST_REG_B(dst_reg, src_reg);
        
        return dst_reg;
}
static int TEST_HOST_REG_W(int dst_reg, int src_reg)
{
        AND_HOST_REG_W(dst_reg, src_reg);
        
        return dst_reg;
}
static int TEST_HOST_REG_L(int dst_reg, int src_reg)
{
        AND_HOST_REG_L(dst_reg, src_reg);
        
        return dst_reg;
}
static int TEST_HOST_REG_IMM(int host_reg, uint32_t imm)
{
        AND_HOST_REG_IMM(host_reg, imm);
        
        return host_reg;
}

#define OR_HOST_REG_B OR_HOST_REG_L
#define OR_HOST_REG_W OR_HOST_REG_L
static void OR_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x09); /*ORL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static void OR_HOST_REG_IMM(int host_reg, uint32_t imm)
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

static void NEG_HOST_REG_B(int reg)
{
        addbyte(0xf6);
        addbyte(0xd8 | reg);
}
static void NEG_HOST_REG_W(int reg)
{
        addbyte(0x66);
        addbyte(0xf7);
        addbyte(0xd8 | reg);
}
static void NEG_HOST_REG_L(int reg)
{
        addbyte(0xf7);
        addbyte(0xd8 | reg);
}

static void SUB_HOST_REG_B(int dst_reg, int src_reg)
{
        addbyte(0x28); /*SUBB dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static void SUB_HOST_REG_W(int dst_reg, int src_reg)
{
        addbyte(0x66); /*SUBW dst_reg, src_reg*/
        addbyte(0x29);
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static void SUB_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x29); /*SUBL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static void SUB_HOST_REG_IMM_B(int host_reg, uint8_t imm)
{
        addbyte(0x80); /*SUBB host_reg, imm*/
        addbyte(0xE8 | host_reg);
        addbyte(imm);
}
static void SUB_HOST_REG_IMM_W(int host_reg, uint16_t imm)
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
static void SUB_HOST_REG_IMM(int host_reg, uint32_t imm)
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

static int CMP_HOST_REG_B(int dst_reg, int src_reg)
{
        SUB_HOST_REG_B(dst_reg, src_reg);
        
        return dst_reg;
}
static int CMP_HOST_REG_W(int dst_reg, int src_reg)
{
        SUB_HOST_REG_W(dst_reg, src_reg);
        
        return dst_reg;
}
static int CMP_HOST_REG_L(int dst_reg, int src_reg)
{
        SUB_HOST_REG_L(dst_reg, src_reg);
        
        return dst_reg;
}
static int CMP_HOST_REG_IMM_B(int host_reg, uint8_t imm)
{
        SUB_HOST_REG_IMM_B(host_reg, imm);
        
        return host_reg;
}
static int CMP_HOST_REG_IMM_W(int host_reg, uint16_t imm)
{
        SUB_HOST_REG_IMM_W(host_reg, imm);
        
        return host_reg;
}
static int CMP_HOST_REG_IMM_L(int host_reg, uint32_t imm)
{
        SUB_HOST_REG_IMM(host_reg, imm);
        
        return host_reg;
}

#define XOR_HOST_REG_B XOR_HOST_REG_L
#define XOR_HOST_REG_W XOR_HOST_REG_L
static void XOR_HOST_REG_L(int dst_reg, int src_reg)
{
        addbyte(0x31); /*XORL dst_reg, src_reg*/
        addbyte(0xc0 | dst_reg | (src_reg << 3));
}
static void XOR_HOST_REG_IMM(int host_reg, uint32_t imm)
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

static void CALL_FUNC(void *dest)
{
        addbyte(0xE8); /*CALL*/
        addlong(((uint8_t *)dest - (uint8_t *)(&codeblock[block_current].data[block_pos + 4])));
}

static void SHL_B_IMM(int reg, int count)
{
        addbyte(0xc0); /*SHL reg, count*/
        addbyte(0xc0 | reg | 0x20);
        addbyte(count);
}
static void SHL_W_IMM(int reg, int count)
{
        addbyte(0x66); /*SHL reg, count*/
        addbyte(0xc1);
        addbyte(0xc0 | reg | 0x20);
        addbyte(count);
}
static void SHL_L_IMM(int reg, int count)
{
        addbyte(0xc1); /*SHL reg, count*/
        addbyte(0xc0 | reg | 0x20);
        addbyte(count);
}
static void SHR_B_IMM(int reg, int count)
{
        addbyte(0xc0); /*SHR reg, count*/
        addbyte(0xc0 | reg | 0x28);
        addbyte(count);
}
static void SHR_W_IMM(int reg, int count)
{
        addbyte(0x66); /*SHR reg, count*/
        addbyte(0xc1);
        addbyte(0xc0 | reg | 0x28);
        addbyte(count);
}
static void SHR_L_IMM(int reg, int count)
{
        addbyte(0xc1); /*SHR reg, count*/
        addbyte(0xc0 | reg | 0x28);
        addbyte(count);
}
static void SAR_B_IMM(int reg, int count)
{
        addbyte(0xc0); /*SAR reg, count*/
        addbyte(0xc0 | reg | 0x38);
        addbyte(count);
}
static void SAR_W_IMM(int reg, int count)
{
        addbyte(0x66); /*SAR reg, count*/
        addbyte(0xc1);
        addbyte(0xc0 | reg | 0x38);
        addbyte(count);
}
static void SAR_L_IMM(int reg, int count)
{
        addbyte(0xc1); /*SAR reg, count*/
        addbyte(0xc0 | reg | 0x38);
        addbyte(count);
}


static void CHECK_SEG_READ(x86seg *seg)
{
        /*Segments always valid in real/V86 mode*/
        if (!(cr0 & 1) || (eflags & VM_FLAG))
                return;
        /*CS and SS must always be valid*/
        if (seg == &_cs || seg == &_ss)
                return;
        if (seg->checked)
                return;

        addbyte(0x83); /*CMP seg->base, -1*/
        addbyte(0x05|0x38);
        addlong((uint32_t)&seg->base);
        addbyte(-1);
        addbyte(0x0f);
        addbyte(0x84); /*JE end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
        
        seg->checked = 1;
}
static void CHECK_SEG_WRITE(x86seg *seg)
{
        /*Segments always valid in real/V86 mode*/
        if (!(cr0 & 1) || (eflags & VM_FLAG))
                return;
        /*CS and SS must always be valid*/
        if (seg == &_cs || seg == &_ss)
                return;
        if (seg->checked)
                return;
                
        addbyte(0x83); /*CMP seg->base, -1*/
        addbyte(0x05|0x38);
        addlong((uint32_t)&seg->base);
        addbyte(-1);
        addbyte(0x0f);
        addbyte(0x84); /*JE end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));

        seg->checked = 1;
}
static void CHECK_SEG_LIMITS(x86seg *seg, int end_offset)
{
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

static void MEM_LOAD_ADDR_EA_B(x86seg *seg)
{
        addbyte(0x8b); /*MOVL EDX, seg->base*/
        addbyte(0x05 | (REG_EDX << 3));
        addlong((uint32_t)&seg->base);
        addbyte(0x89); /*MOV ESI, EDX*/
        addbyte(0xd6);
        addbyte(0x01); /*ADDL EDX, EAX*/
        addbyte(0xc2);
        addbyte(0x89); /*MOV EDI, EDX*/
        addbyte(0xd7);
        addbyte(0xc1); /*SHR EDX, 12*/
        addbyte(0xea);
        addbyte(12);
        addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
        addbyte(0x14);
        addbyte(0x95);
        addlong((uint32_t)readlookup2);
        addbyte(0x83); /*CMP EDX, -1*/
        addbyte(0xfa);
        addbyte(-1);
        addbyte(0x74); /*JE slowpath*/
        addbyte(4+2);
        addbyte(0x0f); /*MOVZX EAX, B[EDX+EDI]*/
        addbyte(0xb6);
        addbyte(0x04);
        addbyte(0x3a);
        addbyte(0xeb); /*JMP done*/
        addbyte(4+3+5+7+6+3);
        addbyte(0x89); /*slowpath: MOV [ESP+4], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0x89); /*MOV [ESP], ESI*/
        addbyte(0x34);
        addbyte(0x24);
        addbyte(0xe8); /*CALL readmemb386l*/
        addlong((uint32_t)readmemb386l - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0x83); /*CMP abrt, 0*/
        addbyte(0x3d);
        addlong((uint32_t)&abrt);
        addbyte(0);
        addbyte(0x0f); /*JNE end*/
        addbyte(0x85);
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
        addbyte(0x0f); /*MOVZX EAX, AL*/
        addbyte(0xb6);
        addbyte(0xc0);
        /*done:*/
        host_reg_mapping[0] = 8;
}
static void MEM_LOAD_ADDR_EA_W(x86seg *seg)
{
        addbyte(0x8b); /*MOVL EDX, seg->base*/
        addbyte(0x05 | (REG_EDX << 3));
        addlong((uint32_t)&seg->base);
        addbyte(0x89); /*MOV ESI, EDX*/
        addbyte(0xd6);
        addbyte(0x01); /*ADDL EDX, EAX*/
        addbyte(0xc2);
        addbyte(0x8d); /*LEA EDI, 1[EDX]*/
        addbyte(0x7a);
        addbyte(0x01);
        addbyte(0xc1); /*SHR EDX, 12*/
        addbyte(0xea);
        addbyte(12);
        addbyte(0xf7); /*TEST EDI, 0xfff*/
        addbyte(0xc7);
        addlong(0xfff);
        addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
        addbyte(0x14);
        addbyte(0x95);
        addlong((uint32_t)readlookup2);
        addbyte(0x74); /*JE slowpath*/
        addbyte(3+2+5+2);
        addbyte(0x83); /*CMP EDX, -1*/
        addbyte(0xfa);
        addbyte(-1);
        addbyte(0x74); /*JE slowpath*/
        addbyte(5+2);
        addbyte(0x0f); /*MOVZX EAX, -1[EDX+EDI]W*/
        addbyte(0xb7);
        addbyte(0x44);
        addbyte(0x3a);
        addbyte(-1);
        addbyte(0xeb); /*JMP done*/
        addbyte(4+3+5+7+6+3);
        addbyte(0x89); /*slowpath: MOV [ESP+4], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0x89); /*MOV [ESP], ESI*/
        addbyte(0x34);
        addbyte(0x24);
        addbyte(0xe8); /*CALL readmemwl*/
        addlong((uint32_t)readmemwl - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0x83); /*CMP abrt, 0*/
        addbyte(0x3d);
        addlong((uint32_t)&abrt);
        addbyte(0);
        addbyte(0x0f);
        addbyte(0x85); /*JNE end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
        addbyte(0x0f); /*MOVZX EAX, AX*/
        addbyte(0xb7);
        addbyte(0xc0);
        /*done:*/
        host_reg_mapping[0] = 8;
}
static void MEM_LOAD_ADDR_EA_L(x86seg *seg)
{
        addbyte(0x8b); /*MOVL EDX, seg->base*/
        addbyte(0x05 | (REG_EDX << 3));
        addlong((uint32_t)&seg->base);
        addbyte(0x89); /*MOV ESI, EDX*/
        addbyte(0xd6);
        addbyte(0x01); /*ADDL EDX, EAX*/
        addbyte(0xc2);
        addbyte(0x8d); /*LEA EDI, 3[EDX]*/
        addbyte(0x7a);
        addbyte(0x03);
        addbyte(0xc1); /*SHR EDX, 12*/
        addbyte(0xea);
        addbyte(12);
        addbyte(0xf7); /*TEST EDI, 0xffc*/
        addbyte(0xc7);
        addlong(0xffc);
        addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
        addbyte(0x14);
        addbyte(0x95);
        addlong((uint32_t)readlookup2);
        addbyte(0x74); /*JE slowpath*/
        addbyte(3+2+4+2);
        addbyte(0x83); /*CMP EDX, -1*/
        addbyte(0xfa);
        addbyte(-1);
        addbyte(0x74); /*JE slowpath*/
        addbyte(4+2);
        addbyte(0x8b); /*MOV EAX, -3[EDX+EDI]*/
        addbyte(0x44);
        addbyte(0x3a);
        addbyte(-3);
        addbyte(0xeb); /*JMP done*/
        addbyte(4+3+5+7+6);
        addbyte(0x89); /*slowpath: MOV [ESP+4], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0x89); /*MOV [ESP], ESI*/
        addbyte(0x34);
        addbyte(0x24);
        addbyte(0xe8); /*CALL readmemll*/
        addlong((uint32_t)readmemll - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0x83); /*CMP abrt, 0*/
        addbyte(0x3d);
        addlong((uint32_t)&abrt);
        addbyte(0);
        addbyte(0x0f);
        addbyte(0x85); /*JNE end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
        /*done:*/
        host_reg_mapping[0] = 8;
}

static void MEM_LOAD_ADDR_EA_Q(x86seg *seg)
{
        addbyte(0x8b); /*MOVL EDX, seg->base*/
        addbyte(0x05 | (REG_EDX << 3));
        addlong((uint32_t)&seg->base);
        addbyte(0x89); /*MOV ESI, EDX*/
        addbyte(0xd6);
        addbyte(0x01); /*ADDL EDX, EAX*/
        addbyte(0xc2);
        addbyte(0x8d); /*LEA EDI, 7[EDX]*/
        addbyte(0x7a);
        addbyte(0x07);
        addbyte(0xc1); /*SHR EDX, 12*/
        addbyte(0xea);
        addbyte(12);
        addbyte(0xf7); /*TEST EDI, 0xff8*/
        addbyte(0xc7);
        addlong(0xff8);
        addbyte(0x8b); /*MOV EDX, readlookup2[EDX*4]*/
        addbyte(0x14);
        addbyte(0x95);
        addlong((uint32_t)readlookup2);
        addbyte(0x74); /*JE slowpath*/
        addbyte(3+2+4+4+2);
        addbyte(0x83); /*CMP EDX, -1*/
        addbyte(0xfa);
        addbyte(-1);
        addbyte(0x74); /*JE slowpath*/
        addbyte(4+4+2);
        addbyte(0x8b); /*MOV EAX, [EDX+EDI]*/
        addbyte(0x44);
        addbyte(0x3a);
        addbyte(-7);
        addbyte(0x8b); /*MOV EDX, [EDX+EDI+4]*/
        addbyte(0x54);
        addbyte(0x3a);
        addbyte(-7+4);
        addbyte(0xeb); /*JMP done*/
        addbyte(4+3+5+7+6);
        addbyte(0x89); /*slowpath: MOV [ESP+4], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0x89); /*MOV [ESP], ESI*/
        addbyte(0x34);
        addbyte(0x24);
        addbyte(0xe8); /*CALL readmemql*/
        addlong((uint32_t)readmemql - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0x83); /*CMP abrt, 0*/
        addbyte(0x3d);
        addlong((uint32_t)&abrt);
        addbyte(0);
        addbyte(0x0f);
        addbyte(0x85); /*JNE end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
        /*done:*/
        host_reg_mapping[0] = 8;
}

static void MEM_LOAD_ADDR_IMM_B(x86seg *seg, uint32_t addr)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_LOAD_ADDR_EA_B(seg);
}
static void MEM_LOAD_ADDR_IMM_W(x86seg *seg, uint32_t addr)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_LOAD_ADDR_EA_W(seg);
}
static void MEM_LOAD_ADDR_IMM_L(x86seg *seg, uint32_t addr)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_LOAD_ADDR_EA_L(seg);
}

static void MEM_STORE_ADDR_EA_B(x86seg *seg, int host_reg)
{
        addbyte(0x8b); /*MOVL ESI, seg->base*/
        addbyte(0x05 | (REG_ESI << 3));
        addlong((uint32_t)&seg->base);
        addbyte(0x01); /*ADDL ESI, EAX*/
        addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
        addbyte(0x89); /*MOV EDI, ESI*/
        addbyte(0xc0 | (REG_ESI << 3) | REG_EDI);
        addbyte(0xc1); /*SHR ESI, 12*/
        addbyte(0xe8 | REG_ESI);
        addbyte(12);
        addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
        addbyte(0x04 | (REG_ESI << 3));
        addbyte(0x85 | (REG_ESI << 3));
        addlong((uint32_t)writelookup2);
        addbyte(0x83); /*CMP ESI, -1*/
        addbyte(0xf8 | REG_ESI);
        addbyte(-1);
        addbyte(0x74); /*JE slowpath*/
        addbyte(3+2);
        addbyte(0x88); /*MOV [EDI+ESI],host_reg*/
        addbyte(0x04 | (host_reg << 3));
        addbyte(REG_EDI | (REG_ESI << 3));
        addbyte(0xeb); /*JMP done*/
        addbyte(4+5+4+3+5+7+6);
        addbyte(0x89); /*slowpath: MOV [ESP+4], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0xa1); /*MOV EAX, seg->base*/
        addlong((uint32_t)&seg->base);
        addbyte(0x89); /*MOV [ESP+8], host_reg*/
        addbyte(0x44 | (host_reg << 3));
        addbyte(0x24);
        addbyte(0x08);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xe8); /*CALL writememb386l*/
        addlong((uint32_t)writememb386l - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0x83); /*CMP abrt, 0*/
        addbyte(0x3d);
        addlong((uint32_t)&abrt);
        addbyte(0);
        addbyte(0x0f); /*JNE end*/
        addbyte(0x85);
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
        /*done:*/
}
static void MEM_STORE_ADDR_EA_W(x86seg *seg, int host_reg)
{
        addbyte(0x8b); /*MOVL ESI, seg->base*/
        addbyte(0x05 | (REG_ESI << 3));
        addlong((uint32_t)&seg->base);
        addbyte(0x01); /*ADDL ESI, EAX*/
        addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
        addbyte(0x8d); /*LEA EDI, 1[ESI]*/
        addbyte(0x7e);
        addbyte(0x01);
        addbyte(0xc1); /*SHR ESI, 12*/
        addbyte(0xe8 | REG_ESI);
        addbyte(12);
        addbyte(0xf7); /*TEST EDI, 0xfff*/
        addbyte(0xc7);
        addlong(0xfff);
        addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
        addbyte(0x04 | (REG_ESI << 3));
        addbyte(0x85 | (REG_ESI << 3));
        addlong((uint32_t)writelookup2);
        addbyte(0x74); /*JE slowpath*/
        addbyte(3+2+5+2);
        addbyte(0x83); /*CMP ESI, -1*/
        addbyte(0xf8 | REG_ESI);
        addbyte(-1);
        addbyte(0x74); /*JE slowpath*/
        addbyte(5+2);
        addbyte(0x66); /*MOV -1[EDI+ESI],host_reg*/
        addbyte(0x89);
        addbyte(0x44 | (host_reg << 3));
        addbyte(REG_EDI | (REG_ESI << 3));
        addbyte(-1);
        addbyte(0xeb); /*JMP done*/
        addbyte(4+5+4+3+5+7+6);
        addbyte(0x89); /*slowpath: MOV [ESP+4], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0xa1); /*MOV EAX, seg->base*/
        addlong((uint32_t)&seg->base);
        addbyte(0x89); /*MOV [ESP+8], host_reg*/
        addbyte(0x44 | (host_reg << 3));
        addbyte(0x24);
        addbyte(0x08);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xe8); /*CALL writememwl*/
        addlong((uint32_t)writememwl - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0x83); /*CMP abrt, 0*/
        addbyte(0x3d);
        addlong((uint32_t)&abrt);
        addbyte(0);
        addbyte(0x0f); /*JNE end*/
        addbyte(0x85);
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
        /*done:*/
}
static void MEM_STORE_ADDR_EA_L(x86seg *seg, int host_reg)
{
        addbyte(0x8b); /*MOVL ESI, seg->base*/
        addbyte(0x05 | (REG_ESI << 3));
        addlong((uint32_t)&seg->base);
        addbyte(0x01); /*ADDL ESI, EAX*/
        addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
        addbyte(0x8d); /*LEA EDI, 3[ESI]*/
        addbyte(0x7e);
        addbyte(0x03);
        addbyte(0xc1); /*SHR ESI, 12*/
        addbyte(0xe8 | REG_ESI);
        addbyte(12);
        addbyte(0xf7); /*TEST EDI, 0xffc*/
        addbyte(0xc7);
        addlong(0xffc);
        addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
        addbyte(0x04 | (REG_ESI << 3));
        addbyte(0x85 | (REG_ESI << 3));
        addlong((uint32_t)writelookup2);
        addbyte(0x74); /*JE slowpath*/
        addbyte(3+2+4+2);
        addbyte(0x83); /*CMP ESI, -1*/
        addbyte(0xf8 | REG_ESI);
        addbyte(-1);
        addbyte(0x74); /*JE slowpath*/
        addbyte(4+2);
        addbyte(0x89); /*MOV -3[EDI+ESI],host_reg*/
        addbyte(0x44 | (host_reg << 3));
        addbyte(REG_EDI | (REG_ESI << 3));
        addbyte(-3);
        addbyte(0xeb); /*JMP done*/
        addbyte(4+5+4+3+5+7+6);
        addbyte(0x89); /*slowpath: MOV [ESP+4], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0xa1); /*MOV EAX, seg->base*/
        addlong((uint32_t)&seg->base);
        addbyte(0x89); /*MOV [ESP+8], host_reg*/
        addbyte(0x44 | (host_reg << 3));
        addbyte(0x24);
        addbyte(0x08);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xe8); /*CALL writememll*/
        addlong((uint32_t)writememll - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0x83); /*CMP abrt, 0*/
        addbyte(0x3d);
        addlong((uint32_t)&abrt);
        addbyte(0);
        addbyte(0x0f); /*JNE end*/
        addbyte(0x85);
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
        /*done:*/
}
static void MEM_STORE_ADDR_EA_Q(x86seg *seg, int host_reg, int host_reg2)
{
        addbyte(0x8b); /*MOVL ESI, seg->base*/
        addbyte(0x05 | (REG_ESI << 3));
        addlong((uint32_t)&seg->base);
        addbyte(0x01); /*ADDL ESI, EAX*/
        addbyte(0xc0 | (REG_EAX << 3) | REG_ESI);
        addbyte(0x8d); /*LEA EDI, 7[ESI]*/
        addbyte(0x7e);
        addbyte(0x07);
        addbyte(0xc1); /*SHR ESI, 12*/
        addbyte(0xe8 | REG_ESI);
        addbyte(12);
        addbyte(0xf7); /*TEST EDI, 0xff8*/
        addbyte(0xc7);
        addlong(0xff8);
        addbyte(0x8b); /*MOV ESI, readlookup2[ESI*4]*/
        addbyte(0x04 | (REG_ESI << 3));
        addbyte(0x85 | (REG_ESI << 3));
        addlong((uint32_t)writelookup2);
        addbyte(0x74); /*JE slowpath*/
        addbyte(3+2+4+4+2);
        addbyte(0x83); /*CMP ESI, -1*/
        addbyte(0xf8 | REG_ESI);
        addbyte(-1);
        addbyte(0x74); /*JE slowpath*/
        addbyte(4+4+2);
        addbyte(0x89); /*MOV [EDI+ESI],host_reg*/
        addbyte(0x44 | (host_reg << 3));
        addbyte(REG_EDI | (REG_ESI << 3));
        addbyte(-7);
        addbyte(0x89); /*MOV [EDI+ESI+4],host_reg2*/
        addbyte(0x44 | (host_reg2 << 3));
        addbyte(REG_EDI | (REG_ESI << 3));
        addbyte(-7+0x04);
        addbyte(0xeb); /*JMP done*/
        addbyte(4+5+4+4+3+5+7+6);
        addbyte(0x89); /*slowpath: MOV [ESP+4], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0xa1); /*MOV EAX, seg->base*/
        addlong((uint32_t)&seg->base);
        addbyte(0x89); /*MOV [ESP+8], host_reg*/
        addbyte(0x44 | (host_reg << 3));
        addbyte(0x24);
        addbyte(0x08);
        addbyte(0x89); /*MOV [ESP+12], host_reg2*/
        addbyte(0x44 | (host_reg2 << 3));
        addbyte(0x24);
        addbyte(0x0c);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xe8); /*CALL writememql*/
        addlong((uint32_t)writememql - (uint32_t)(&codeblock[block_current].data[block_pos + 4]));
        addbyte(0x83); /*CMP abrt, 0*/
        addbyte(0x3d);
        addlong((uint32_t)&abrt);
        addbyte(0);
        addbyte(0x0f); /*JNE end*/
        addbyte(0x85);
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
        /*done:*/
}

static void MEM_STORE_ADDR_IMM_B(x86seg *seg, uint32_t addr, int host_reg)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_STORE_ADDR_EA_B(seg, host_reg);
}
static void MEM_STORE_ADDR_IMM_L(x86seg *seg, uint32_t addr, int host_reg)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_STORE_ADDR_EA_L(seg, host_reg);
}
static void MEM_STORE_ADDR_IMM_W(x86seg *seg, uint32_t addr, int host_reg)
{
        addbyte(0xb8); /*MOV EAX, addr*/
        addlong(addr);
        MEM_STORE_ADDR_EA_W(seg, host_reg);
}


static x86seg *FETCH_EA_16(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc)
{
        int mod = (fetchdat >> 6) & 3;
        int reg = (fetchdat >> 3) & 7;
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
                        addlong((fetchdat >> 8) & 0xffff);// pc++;
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
                        op_ea_seg = &_ss;
        }
        return op_ea_seg;
}

static x86seg *FETCH_EA_32(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, int stack_offset)
{
        uint32_t new_eaaddr;
        int mod = (fetchdat >> 6) & 3;
        int reg = (fetchdat >> 3) & 7;
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
                                addlong(new_eaaddr);// pc++;
                                (*op_pc) += 4;
                        }
                        else
                        {
                                addbyte(0x8b); /*MOVL EAX, regs[sib&7].l*/
                                addbyte(0x45);
                                addbyte((uint32_t)&cpu_state.regs[sib & 7].l - (uint32_t)&EAX);
                        }
                        break;
                        case 1: 
                        addbyte(0x8b); /*MOVL EAX, regs[sib&7].l*/
                        addbyte(0x45);
                        addbyte((uint32_t)&cpu_state.regs[sib & 7].l - (uint32_t)&EAX);
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
                        addbyte((uint32_t)&cpu_state.regs[sib & 7].l - (uint32_t)&EAX);
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
                        op_ea_seg = &_ss;
                if (((sib >> 3) & 7) != 4)
                {
                        switch (sib >> 6)
                        {
                                case 0:
                                addbyte(0x03); /*ADDL EAX, regs[sib&7].l*/
                                addbyte(0x45);
                                addbyte((uint32_t)&cpu_state.regs[(sib >> 3) & 7].l - (uint32_t)&EAX);
                                break;
                                case 1:
                                addbyte(0x8B); addbyte(0x45 | (REG_EDI << 3)); addbyte((uint32_t)&cpu_state.regs[(sib >> 3) & 7].l - (uint32_t)&EAX); /*MOVL EDI, reg*/
                                addbyte(0x01); addbyte(0xc0 | REG_EAX | (REG_EDI << 3)); /*ADDL EAX, EDI*/
                                addbyte(0x01); addbyte(0xc0 | REG_EAX | (REG_EDI << 3)); /*ADDL EAX, EDI*/
                                break;
                                case 2:
                                addbyte(0x8B); addbyte(0x45 | (REG_EDI << 3)); addbyte((uint32_t)&cpu_state.regs[(sib >> 3) & 7].l - (uint32_t)&EAX); /*MOVL EDI, reg*/
                                addbyte(0xC1); addbyte(0xE0 | REG_EDI); addbyte(2); /*SHL EDI, 2*/
                                addbyte(0x01); addbyte(0xc0 | REG_EAX | (REG_EDI << 3)); /*ADDL EAX, EDI*/
                                break;
                                case 3:
                                addbyte(0x8B); addbyte(0x45 | (REG_EDI << 3)); addbyte((uint32_t)&cpu_state.regs[(sib >> 3) & 7].l - (uint32_t)&EAX); /*MOVL EDI reg*/
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
                addbyte((uint32_t)&cpu_state.regs[rm].l - (uint32_t)&EAX);
                eaaddr = cpu_state.regs[rm].l;
                if (mod) 
                {
                        if (rm == 5 && !op_ssegs)
                                op_ea_seg = &_ss;
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

static x86seg *FETCH_EA(x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, uint32_t op_32)
{
        if (op_32 & 0x200)
                return FETCH_EA_32(op_ea_seg, fetchdat, op_ssegs, op_pc, 0);
        return FETCH_EA_16(op_ea_seg, fetchdat, op_ssegs, op_pc);
}


static void LOAD_STACK_TO_EA(int off)
{
        if (stack32)
        {
                addbyte(0x8b); /*MOVL EAX,[ESP]*/
                addbyte(0x45 | (REG_EAX << 3));
                addbyte((uint32_t)&ESP - (uint32_t)&EAX);
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
                addbyte((uint32_t)&ESP - (uint32_t)&EAX);
                if (off)
                {
                        addbyte(0x66); /*ADD AX, off*/
                        addbyte(0x05);
                        addword(off);
                }
        }
}

static void LOAD_EBP_TO_EA(int off)
{
        if (stack32)
        {
                addbyte(0x8b); /*MOVL EAX,[EBP]*/
                addbyte(0x45 | (REG_EAX << 3));
                addbyte((uint32_t)&EBP - (uint32_t)&EAX);
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
                addbyte((uint32_t)&EBP - (uint32_t)&EAX);
                if (off)
                {
                        addbyte(0x66); /*ADD AX, off*/
                        addbyte(0x05);
                        addword(off);
                }
        }
}

static void SP_MODIFY(int off)
{
        if (stack32)
        {
                if (off < 0x80)
                {
                        addbyte(0x83); /*ADD [ESP], off*/
                        addbyte(0x45);
                        addbyte((uint32_t)&ESP - (uint32_t)&EAX);
                        addbyte(off);
                }
                else
                {
                        addbyte(0x81); /*ADD [ESP], off*/
                        addbyte(0x45);
                        addbyte((uint32_t)&ESP - (uint32_t)&EAX);
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
                        addbyte((uint32_t)&SP - (uint32_t)&EAX);
                        addbyte(off);
                }
                else
                {
                        addbyte(0x66); /*ADD [SP], off*/
                        addbyte(0x81);
                        addbyte(0x45);
                        addbyte((uint32_t)&SP - (uint32_t)&EAX);
                        addword(off);
                }
        }
}


static void TEST_ZERO_JUMP_W(int host_reg, uint32_t new_pc, int taken_cycles)
{
        addbyte(0x66); /*CMPW host_reg, 0*/
        addbyte(0x83);
        addbyte(0xc0 | 0x38 | host_reg);
        addbyte(0);
        addbyte(0x75); /*JNZ +*/
        addbyte(7+5+(taken_cycles ? 7 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uintptr_t)&cpu_state.pc - (uintptr_t)&cpu_state);
        addlong(new_pc);
        if (taken_cycles)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x2d);
                addlong((uintptr_t)&cycles);
                addbyte(taken_cycles);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}
static void TEST_ZERO_JUMP_L(int host_reg, uint32_t new_pc, int taken_cycles)
{
        addbyte(0x83); /*CMPW host_reg, 0*/
        addbyte(0xc0 | 0x38 | host_reg);
        addbyte(0);
        addbyte(0x75); /*JNZ +*/
        addbyte(7+5+(taken_cycles ? 7 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uintptr_t)&cpu_state.pc - (uintptr_t)&cpu_state);
        addlong(new_pc);
        if (taken_cycles)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x2d);
                addlong((uintptr_t)&cycles);
                addbyte(taken_cycles);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}

static void TEST_NONZERO_JUMP_W(int host_reg, uint32_t new_pc, int taken_cycles)
{
        addbyte(0x66); /*CMPW host_reg, 0*/
        addbyte(0x83);
        addbyte(0xc0 | 0x38 | host_reg);
        addbyte(0);
        addbyte(0x74); /*JZ +*/
        addbyte(7+5+(taken_cycles ? 7 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uintptr_t)&cpu_state.pc - (uintptr_t)&cpu_state);
        addlong(new_pc);
        if (taken_cycles)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x2d);
                addlong((uintptr_t)&cycles);
                addbyte(taken_cycles);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}
static void TEST_NONZERO_JUMP_L(int host_reg, uint32_t new_pc, int taken_cycles)
{
        addbyte(0x83); /*CMPW host_reg, 0*/
        addbyte(0xc0 | 0x38 | host_reg);
        addbyte(0);
        addbyte(0x74); /*JZ +*/
        addbyte(7+5+(taken_cycles ? 7 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uintptr_t)&cpu_state.pc - (uintptr_t)&cpu_state);
        addlong(new_pc);
        if (taken_cycles)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x2d);
                addlong((uintptr_t)&cycles);
                addbyte(taken_cycles);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}

static int BRANCH_COND_BE(int pc_offset, uint32_t op_pc, uint32_t offset, int not)
{
        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_SUB8:
                addbyte(0x8a); /*MOV AL, flags_op1*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op1 - (uintptr_t)&cpu_state);
                addbyte(0x3a); /*CMP AL, flags_op2*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op2 - (uintptr_t)&cpu_state);
                if (not)
                        addbyte(0x76); /*JBE*/
                else
                        addbyte(0x77); /*JNBE*/
                break;
                case FLAGS_SUB16:
                addbyte(0x66); /*MOV AX, flags_op1*/
                addbyte(0x8b);
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op1 - (uintptr_t)&cpu_state);
                addbyte(0x66); /*CMP AX, flags_op2*/
                addbyte(0x3b);
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op2 - (uintptr_t)&cpu_state);
                if (not)
                        addbyte(0x76); /*JBE*/
                else
                        addbyte(0x77); /*JNBE*/
                break;
                case FLAGS_SUB32:
                addbyte(0x8b); /*MOV EAX, flags_op1*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op1 - (uintptr_t)&cpu_state);
                addbyte(0x3b); /*CMP EAX, flags_op2*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op2 - (uintptr_t)&cpu_state);
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
                        addbyte((uintptr_t)&cpu_state.flags_res - (uintptr_t)&cpu_state);
                        addbyte(0);
                        addbyte(0x74); /*JZ +*/
                }
                else
                {
                        CALL_FUNC(ZF_SET);
                        addbyte(0x85); /*TEST EAX,EAX*/
                        addbyte(0xc0);
                        addbyte(0x75); /*JNZ +*/
                }
                if (not)
                        addbyte(5+2+2+7+5+(timing_bt ? 7 : 0));
                else
                        addbyte(5+2+2);
                CALL_FUNC(CF_SET);
                addbyte(0x85); /*TEST EAX,EAX*/
                addbyte(0xc0);
                if (not)
                        addbyte(0x75); /*JNZ +*/
                else
                        addbyte(0x74); /*JZ +*/
                break;
        }
        addbyte(7+5+(timing_bt ? 7 : 0));        
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uintptr_t)&cpu_state.pc - (uintptr_t)&cpu_state);
        addlong(op_pc+pc_offset+offset);
        if (timing_bt)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x2d);
                addlong((uintptr_t)&cycles);
                addbyte(timing_bt);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}

static int BRANCH_COND_L(int pc_offset, uint32_t op_pc, uint32_t offset, int not)
{
        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_SUB8:
                addbyte(0x8a); /*MOV AL, flags_op1*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op1 - (uintptr_t)&cpu_state);
                addbyte(0x3a); /*CMP AL, flags_op2*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op2 - (uintptr_t)&cpu_state);
                if (not)
                        addbyte(0x7c); /*JL*/
                else
                        addbyte(0x7d); /*JNL*/
                break;
                case FLAGS_SUB16:
                addbyte(0x66); /*MOV AX, flags_op1*/
                addbyte(0x8b);
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op1 - (uintptr_t)&cpu_state);
                addbyte(0x66); /*CMP AX, flags_op2*/
                addbyte(0x3b);
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op2 - (uintptr_t)&cpu_state);
                if (not)
                        addbyte(0x7c); /*JL*/
                else
                        addbyte(0x7d); /*JNL*/
                break;
                case FLAGS_SUB32:
                addbyte(0x8b); /*MOV EAX, flags_op1*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op1 - (uintptr_t)&cpu_state);
                addbyte(0x3b); /*CMP EAX, flags_op2*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op2 - (uintptr_t)&cpu_state);
                if (not)
                        addbyte(0x7c); /*JL*/
                else
                        addbyte(0x7d); /*JNL*/
                break;

                default:
                CALL_FUNC(NF_SET);
                addbyte(0x85); /*TEST EAX,EAX*/
                addbyte(0xc0);
                addbyte(0x0f); /*SETNE BL*/
                addbyte(0x95);
                addbyte(0xc3);
                CALL_FUNC(VF_SET);
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
        addbyte(7+5+(timing_bt ? 7 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uintptr_t)&cpu_state.pc - (uintptr_t)&cpu_state);
        addlong(op_pc+pc_offset+offset);
        if (timing_bt)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x2d);
                addlong((uintptr_t)&cycles);
                addbyte(timing_bt);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}

static int BRANCH_COND_LE(int pc_offset, uint32_t op_pc, uint32_t offset, int not)
{
        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_SUB8:
                addbyte(0x8a); /*MOV AL, flags_op1*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op1 - (uintptr_t)&cpu_state);
                addbyte(0x3a); /*CMP AL, flags_op2*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op2 - (uintptr_t)&cpu_state);
                if (not)
                        addbyte(0x7e); /*JLE*/
                else
                        addbyte(0x7f); /*JNLE*/
                break;
                case FLAGS_SUB16:
                addbyte(0x66); /*MOV AX, flags_op1*/
                addbyte(0x8b);
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op1 - (uintptr_t)&cpu_state);
                addbyte(0x66); /*CMP AX, flags_op2*/
                addbyte(0x3b);
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op2 - (uintptr_t)&cpu_state);
                if (not)
                        addbyte(0x7e); /*JLE*/
                else
                        addbyte(0x7f); /*JNLE*/
                break;
                case FLAGS_SUB32:
                addbyte(0x8b); /*MOV EAX, flags_op1*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op1 - (uintptr_t)&cpu_state);
                addbyte(0x3b); /*CMP EAX, flags_op2*/
                addbyte(0x45);
                addbyte((uintptr_t)&cpu_state.flags_op2 - (uintptr_t)&cpu_state);
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
                        addbyte((uintptr_t)&cpu_state.flags_res - (uintptr_t)&cpu_state);
                        addbyte(0);
                        addbyte(0x74); /*JZ +*/
                }
                else
                {
                        CALL_FUNC(ZF_SET);
                        addbyte(0x85); /*TEST EAX,EAX*/
                        addbyte(0xc0);
                        addbyte(0x75); /*JNZ +*/
                }
                if (not)
                        addbyte(5+2+3+5+2+3+2+2+7+5+(timing_bt ? 7 : 0));
                else
                        addbyte(5+2+3+5+2+3+2+2);

                CALL_FUNC(NF_SET);
                addbyte(0x85); /*TEST EAX,EAX*/
                addbyte(0xc0);
                addbyte(0x0f); /*SETNE BL*/
                addbyte(0x95);
                addbyte(0xc3);
                CALL_FUNC(VF_SET);
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
        addbyte(7+5+(timing_bt ? 7 : 0));
        addbyte(0xC7); /*MOVL [pc], new_pc*/
        addbyte(0x45);
        addbyte((uintptr_t)&cpu_state.pc - (uintptr_t)&cpu_state);
        addlong(op_pc+pc_offset+offset);
        if (timing_bt)
        {
                addbyte(0x83); /*SUB $codegen_block_cycles, cyclcs*/
                addbyte(0x2d);
                addlong((uintptr_t)&cycles);
                addbyte(timing_bt);
        }
        addbyte(0xe9); /*JMP end*/
        addlong(BLOCK_EXIT_OFFSET - (block_pos + 4));
}


static void FP_ENTER()
{
        if (codegen_fpu_entered)
                return;
                
        addbyte(0xf6); /*TEST cr0, 0xc*/
        addbyte(0x05);
        addlong((uintptr_t)&cr0);
        addbyte(0xc);
        addbyte(0x74); /*JZ +*/
        addbyte(10+7+5+5);
        addbyte(0xC7); /*MOVL [oldpc],op_old_pc*/
        addbyte(0x05);
        addlong((uintptr_t)&oldpc);
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

static void FP_FLD(int reg)
{
        addbyte(0xa1); /*MOV EAX, [TOP]*/
        addlong((uintptr_t)&TOP);
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
        addbyte(0x04);
        addbyte(0xc5);
        addlong((uintptr_t)ST);
        addbyte(0x83); /*AND EBX, 7*/
        addbyte(0xe3);
        addbyte(0x07);
        addbyte(0x8b); /*MOV EDX, [ST_i64+EAX]*/
        addbyte(0x14);
        addbyte(0xc5);
        addlong((uintptr_t)ST_i64);
        addbyte(0x8b); /*MOV ECX, [ST_i64+4+EAX]*/
        addbyte(0x0c);
        addbyte(0xc5);
        addlong(((uintptr_t)ST_i64) + 4);
        addbyte(0x8a); /*MOV AL, [tag+EAX]*/
        addbyte(0x80);
        addlong((uintptr_t)tag);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x88); /*MOV [tag+EBX], AL*/
        addbyte(0x83);
        addlong((uintptr_t)tag);
        addbyte(0x89); /*MOV [ST_i64+EBX], EDX*/
        addbyte(0x14);
        addbyte(0xdd);
        addlong((uintptr_t)ST_i64);
        addbyte(0x89); /*MOV [ST_i64+EBX+4], ECX*/
        addbyte(0x0c);
        addbyte(0xdd);
        addlong(((uintptr_t)ST_i64) + 4);

        addbyte(0x89); /*MOV [TOP], EBX*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
}

static void FP_FST(int reg)
{
        addbyte(0xa1); /*MOV EAX, [TOP]*/
        addlong((uintptr_t)&TOP);
        addbyte(0xdd); /*FLD [ST+EAX*8]*/
        addbyte(0x04);
        addbyte(0xc5);
        addlong((uintptr_t)ST);
        addbyte(0x88); /*MOV BL, [tag+EAX]*/
        addbyte(0x98);
        addlong((uintptr_t)tag);

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
        addbyte(0x1c);
        addbyte(0xc5);
        addlong((uintptr_t)ST);
        addbyte(0x8a); /*MOV [tag+EAX], BL*/
        addbyte(0x98);
        addlong((uintptr_t)tag);
}

static void FP_FXCH(int reg)
{
        addbyte(0xa1); /*MOV EAX, [TOP]*/
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV EBX, EAX*/
        addbyte(0xc3);
        addbyte(0x83); /*ADD EAX, reg*/
        addbyte(0xc0);
        addbyte(reg);
//#if 0
        addbyte(0xdd); /*FLD [ST+EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x83); /*AND EAX, 7*/
        addbyte(0xe0);
        addbyte(0x07);
        addbyte(0xdd); /*FLD [ST+EAX*8]*/
        addbyte(0x04);
        addbyte(0xc5);
        addlong((uintptr_t)ST);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0xdd); /*FSTP [ST+EAX*8]*/
        addbyte(0x1c);
        addbyte(0xc5);
        addlong((uintptr_t)ST);
        addbyte(0xbe); /*MOVL ESI, tag*/
        addlong((uintptr_t)tag);
        addbyte(0x8a); /*MOV CL, tag[EAX]*/
        addbyte(0x0c);
        addbyte(0x06);
        addbyte(0x8a); /*MOV DL, tag[EBX]*/
        addbyte(0x14);
        addbyte(0x1e);
        addbyte(0x88); /*MOV tag[EBX], CL*/
        addbyte(0x0c);
        addbyte(0x1e);
        addbyte(0x88); /*MOV tag[EAX], DL*/
        addbyte(0x14);
        addbyte(0x06);
        addbyte(0xbe); /*MOVL ESI, ST_int64*/
        addlong((uintptr_t)ST_i64);
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
        reg = reg;
//#endif
#if 0
        addbyte(0xbe); /*MOVL ESI, ST*/
        addlong((uintptr_t)ST);
        
        addbyte(0x8b); /*MOVL EDX, [ESI+EBX*8]*/
        addbyte(0x14);
        addbyte(0xde);
        addbyte(0x83); /*AND EAX, 7*/
        addbyte(0xe0);
        addbyte(0x07);
        addbyte(0x8b); /*MOVL ECX, [ESI+EAX*8]*/
        addbyte(0x0c);
        addbyte(0xc6);
        addbyte(0x89); /*MOVL [ESI+EBX*8], ECX*/
        addbyte(0x0c);
        addbyte(0xde);
        addbyte(0x89); /*MOVL [ESI+EAX*8], EDX*/
        addbyte(0x14);
        addbyte(0xc6);

        addbyte(0x8b); /*MOVL ECX, [4+ESI+EAX*8]*/
        addbyte(0x4c);
        addbyte(0xc6);
        addbyte(0x04);
        addbyte(0x8b); /*MOVL EDX, [4+ESI+EBX*8]*/
        addbyte(0x54);
        addbyte(0xde);
        addbyte(0x04);
        addbyte(0x89); /*MOVL [4+ESI+EBX*8], ECX*/
        addbyte(0x4c);
        addbyte(0xde);
        addbyte(0x04);
        addbyte(0x89); /*MOVL [4+ESI+EAX*8], EDX*/
        addbyte(0x54);
        addbyte(0xc6);
        addbyte(0x04);
#endif
}


static void FP_LOAD_S()
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
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
        addbyte(0x83); /*CMP EAX, 0*/
        addbyte(0xf8);
        addbyte(0);
        addbyte(0x89); /*MOV TOP, EBX*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x0f); /*SETE [tag+EBX]*/
        addbyte(0x94);
        addbyte(0x83);
        addlong((uintptr_t)tag);
}
static void FP_LOAD_D()
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
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
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x0f); /*SETE [tag+EBX]*/
        addbyte(0x94);
        addbyte(0x83);
        addlong((uintptr_t)tag);
}
static void FP_LOAD_IW()
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
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
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x0f); /*SETE [tag+EBX]*/
        addbyte(0x94);
        addbyte(0x83);
        addlong((uintptr_t)tag);
}
static void FP_LOAD_IL()
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
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
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x0f); /*SETE [tag+EBX]*/
        addbyte(0x94);
        addbyte(0x83);
        addlong((uintptr_t)tag);
}
static void FP_LOAD_IQ()
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x83); /*SUB EBX, 1*/
        addbyte(0xeb);
        addbyte(1);
        addbyte(0x83); /*AND EBX, 7*/
        addbyte(0xe3);
        addbyte(7);
        addbyte(0x89); /*MOV [ST_i64+EBX*8], EAX*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uint32_t)ST_i64);
        addbyte(0x09); /*OR EAX, EDX*/
        addbyte(0xd0);
        addbyte(0x89); /*MOV [ST_i64+4+EBX*8], EDX*/
        addbyte(0x14);
        addbyte(0xdd);
        addlong(((uint32_t)ST_i64) + 4);
        addbyte(0x83); /*CMP EAX, 0*/
        addbyte(0xf8);
        addbyte(0);
        addbyte(0xdf); /*FILDl [ST_i64+EBX*8]*/
        addbyte(0x2c);
        addbyte(0xdd);
        addlong((uint32_t)ST_i64);
        addbyte(0x0f); /*SETE AL*/
        addbyte(0x94);
        addbyte(0xc0);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x0c); /*OR AL, TAG_UINT64*/
        addbyte(TAG_UINT64);
        addbyte(0x89); /*MOV TOP, EBX*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x88); /*MOV [tag+EBX], AL*/
        addbyte(0x83);
        addlong((uint32_t)tag);
}

static int FP_LOAD_REG(int reg)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
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
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0xd9); /*FSTP [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);
        addbyte(0x8b); /*MOV EAX, [ESP]*/
        addbyte(0x04 | (REG_EBX << 3));
        addbyte(0x24);
        
        return REG_EBX;
}

static void FP_LOAD_REG_D(int reg, int *host_reg1, int *host_reg2)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
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
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
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


static double _fp_half = 0.5;

static void FP_LOAD_ROUNDING()
{
        pclog("npxc %04x\n", npxc);
        addbyte(0x8b); /*MOV EDX, npxc*/
        addbyte(0x15);
        addlong((uintptr_t)&npxc);
        addbyte(0xd9); /*FSTCW [ESP+8]*/
        addbyte(0x7c);
        addbyte(0x24);
        addbyte(0x08);
        addbyte(0x89); /*MOV [ESP+12],EDX*/
        addbyte(0x54);
        addbyte(0x24);
        addbyte(0x0c);
        addbyte(0xd9); /*FLDCW [ESP+12]*/
        addbyte(0x6c);
        addbyte(0x24);
        addbyte(0x0c);
}
static void FP_RESTORE_ROUNDING()
{
        addbyte(0xd9); /*FLDCW [ESP+8]*/
        addbyte(0x6c);
        addbyte(0x24);
        addbyte(0x08);
}

static int32_t x87_fround32(double b)
{
        int64_t a, c;
        
        switch ((npxc>>10)&3)
        {
                case 0: /*Nearest*/
                a = (int64_t)floor(b);
                c = (int64_t)floor(b + 1.0);
                if ((b - a) < (c - b))
                        return a;
                else if ((b - a) > (c - b))
                        return c;
                else
                        return (a & 1) ? c : a;
                case 1: /*Down*/
                return (int32_t)floor(b);
                case 2: /*Up*/
                return (int32_t)ceil(b);
                case 3: /*Chop*/
                return (int32_t)b;
        }
}
static int64_t x87_fround64(double b)
{
        int64_t a, c;
        
        switch ((npxc>>10)&3)
        {
                case 0: /*Nearest*/
                a = (int64_t)floor(b);
                c = (int64_t)floor(b + 1.0);
                if ((b - a) < (c - b))
                        return a;
                else if ((b - a) > (c - b))
                        return c;
                else
                        return (a & 1) ? c : a;
                case 1: /*Down*/
                return (int64_t)floor(b);
                case 2: /*Up*/
                return (int64_t)ceil(b);
                case 3: /*Chop*/
                return (int64_t)b;
        }
}
static int FP_LOAD_REG_INT_W(int reg)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
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
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        
        addbyte(0x89); /*MOV [ESP+8], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x08);
        
        addbyte(0xdd); /*FSTP [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);

        CALL_FUNC(x87_fround32);
        
        addbyte(0x89); /*MOV EBX, EAX*/
        addbyte(0xc3);
        
        addbyte(0x8b); /*MOV EAX, [ESP+8]*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x08);
        
        return REG_EBX;
}
static int FP_LOAD_REG_INT(int reg)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
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
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);

        addbyte(0x89); /*MOV [ESP+8], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x08);
        
        addbyte(0xdd); /*FSTP [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);

        CALL_FUNC(x87_fround32);
        
        addbyte(0x89); /*MOV EBX, EAX*/
        addbyte(0xc3);
        
        addbyte(0x8b); /*MOV EAX, [ESP+8]*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x08);
        
        return REG_EBX;
}
static void FP_LOAD_REG_INT_Q(int reg, int *host_reg1, int *host_reg2)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        if (reg)
        {
                addbyte(0x83); /*ADD EBX, reg*/
                addbyte(0xc3);
                addbyte(reg);
                addbyte(0x83); /*AND EBX, 7*/
                addbyte(0xe3);
                addbyte(7);
        }
        if (codegen_fpu_loaded_iq[TOP] && (tag[TOP] & TAG_UINT64))
        {
                /*If we know the register was loaded with FILDq in this block and
                  has not been modified, then we can skip most of the conversion
                  and just load the 64-bit integer representation directly */
                addbyte(0x8b); /*MOV ECX, [ST_i64+EBX*8]*/
                addbyte(0x0c);
                addbyte(0xdd);
                addlong((uint32_t)ST_i64+4);
                addbyte(0x8b); /*MOV EBX, [ST_i64+EBX*8]*/
                addbyte(0x1c);
                addbyte(0xdd);
                addlong((uint32_t)ST_i64);
                
                return;
        }
        
        addbyte(0xf6); /*TEST TAG[EBX], TAG_UINT64*/
        addbyte(0x83);
        addlong((uintptr_t)tag);
        addbyte(TAG_UINT64);
        addbyte(0x74); /*JZ +*/
        addbyte(7+7+2);

        addbyte(0x8b); /*MOV ECX, [ST_i64+EBX*8]*/
        addbyte(0x0c);
        addbyte(0xdd);
        addlong((uint32_t)ST_i64+4);
        addbyte(0x8b); /*MOV EBX, [ST_i64+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uint32_t)ST_i64);
        
        addbyte(0xeb); /*JMP done*/
        addbyte(7+4+3+5+2+2+4);
        
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);

        addbyte(0x89); /*MOV [ESP+8], EAX*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x08);
        
        addbyte(0xdd); /*FSTP [ESP]*/
        addbyte(0x1c);
        addbyte(0x24);

        CALL_FUNC(x87_fround64);
        
        addbyte(0x89); /*MOV EBX, EAX*/
        addbyte(0xc3);
        
        addbyte(0x89); /*MOV ECX, EDX*/
        addbyte(0xd1);

        addbyte(0x8b); /*MOV EAX, [ESP+8]*/
        addbyte(0x44);
        addbyte(0x24);
        addbyte(0x08);

        *host_reg1 = REG_EBX;
        *host_reg2 = REG_ECX;
}

static void FP_POP()
{
        addbyte(0xa1); /*MOV EAX, TOP*/
        addlong((uintptr_t)&TOP);
        addbyte(0xc6); /*MOVB tag[EAX], 3*/
        addbyte(0x80);
        addlong((uintptr_t)tag);
        addbyte(3);
        addbyte(0x04); /*ADD AL, 1*/
        addbyte(1);
        addbyte(0x24); /*AND AL, 7*/
        addbyte(7);
        addbyte(0xa2); /*MOV TOP, AL*/
        addlong((uintptr_t)&TOP);
}

#define FPU_ADD  0x00
#define FPU_DIV  0x30
#define FPU_DIVR 0x38
#define FPU_MUL  0x08
#define FPU_SUB  0x20
#define FPU_SUBR 0x28

static void FP_OP_S(int op)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
        addbyte(0xa3);
        addlong((uintptr_t)tag);
        addbyte(~TAG_UINT64);
        addbyte(0xd8); /*FADD [ESP]*/
        addbyte(0x04 | op);
        addbyte(0x24);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
}
static void FP_OP_D(int op)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        if (((npxc >> 10) & 3) && op == FPU_ADD)
        {
                addbyte(0x9b); /*FSTCW [ESP+8]*/
                addbyte(0xd9);
                addbyte(0x7c);
                addbyte(0x24);
                addbyte(0x08);
                addbyte(0x66); /*MOV AX, [ESP+8]*/
                addbyte(0x8b);
                addbyte(0x44);
                addbyte(0x24);
                addbyte(0x08);
                addbyte(0x66); /*AND AX, ~(3 << 10)*/
                addbyte(0x25);
                addword(~(3 << 10));
                addbyte(0x66); /*OR AX, npxc & (3 << 10)*/
                addbyte(0x0d);
                addword(npxc & (3 << 10));
                addbyte(0x66); /*MOV [ESP+12], AX*/
                addbyte(0x89);
                addbyte(0x44);
                addbyte(0x24);
                addbyte(0x0c);
                addbyte(0xd9); /*FLDCW [ESP+12]*/
                addbyte(0x6c);
                addbyte(0x24);
                addbyte(0x0c);
        }
        addbyte(0x89); /*MOV [ESP+4], EDX*/
        addbyte(0x54);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
        addbyte(0xa3);
        addlong((uintptr_t)tag);
        addbyte(~TAG_UINT64);
        addbyte(0xdc); /*FADD [ESP]*/
        addbyte(0x04 | op);
        addbyte(0x24);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        if (((npxc >> 10) & 3) && op == FPU_ADD)
        {
                addbyte(0xd9); /*FLDCW [ESP+8]*/
                addbyte(0x6c);
                addbyte(0x24);
                addbyte(0x08);
        }        
}
static void FP_OP_IW(int op)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
        addbyte(0xa3);
        addlong((uintptr_t)tag);
        addbyte(~TAG_UINT64);
        addbyte(0xde); /*FADD [ESP]*/
        addbyte(0x04 | op);
        addbyte(0x24);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
}
static void FP_OP_IL(int op)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
        addbyte(0xa3);
        addlong((uintptr_t)tag);
        addbyte(~TAG_UINT64);
        addbyte(0xda); /*FADD [ESP]*/
        addbyte(0x04 | op);
        addbyte(0x24);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
}
static void FP_OP_IQ(int op)
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0x89); /*MOV [ESP+4], EDX*/
        addbyte(0x54);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
        addbyte(0xa3);
        addlong((uintptr_t)tag);
        addbyte(~TAG_UINT64);
        addbyte(0xdc); /*FADD [ESP]*/
        addbyte(0x04 | op);
        addbyte(0x24);
        addbyte(0xdd); /*FSTP [ST+EBX*8]*/
        addbyte(0x1c);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
}

#define C0 (1<<8)
#define C1 (1<<9)
#define C2 (1<<10)
#define C3 (1<<14)

static void FP_COMPARE_S()
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x8a); /*MOV BL, [npxs+1]*/
        addbyte(0x1d);
        addlong(((uintptr_t)&npxs) + 1);
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
        addbyte(0x1d);
        addlong(((uintptr_t)&npxs) + 1);
}
static void FP_COMPARE_D()
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0x89); /*MOV [ESP+4], EDX*/
        addbyte(0x54);
        addbyte(0x24);
        addbyte(0x04);
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x8a); /*MOV BL, [npxs+1]*/
        addbyte(0x1d);
        addlong(((uintptr_t)&npxs) + 1);
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
        addbyte(0x1d);
        addlong(((uintptr_t)&npxs) + 1);
}
static void FP_COMPARE_IW()
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x8a); /*MOV BL, [npxs+1]*/
        addbyte(0x1d);
        addlong(((uintptr_t)&npxs) + 1);
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
        addbyte(0x1d);
        addlong(((uintptr_t)&npxs) + 1);
}
static void FP_COMPARE_IL()
{
        addbyte(0x8b); /*MOV EBX, TOP*/
        addbyte(0x1d);
        addlong((uintptr_t)&TOP);
        addbyte(0x89); /*MOV [ESP], EAX*/
        addbyte(0x04);
        addbyte(0x24);
        addbyte(0xdd); /*FLD ST[EBX*8]*/
        addbyte(0x04);
        addbyte(0xdd);
        addlong((uintptr_t)ST);
        addbyte(0x8a); /*MOV BL, [npxs+1]*/
        addbyte(0x1d);
        addlong(((uintptr_t)&npxs) + 1);
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
        addbyte(0x1d);
        addlong(((uintptr_t)&npxs) + 1);
}

static void FP_OP_REG(int op, int dst, int src)
{
        addbyte(0xa1); /*MOV EAX, TOP*/
        addlong((uintptr_t)&TOP);
        addbyte(0xbe); /*MOVL ESI, ST*/
        addlong((uintptr_t)ST);
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
                addbyte(0x04);
                addbyte(0xde);
                addbyte(0x80); /*AND tag[EBX], ~TAG_UINT64*/
                addbyte(0xa3);
                addlong((uintptr_t)tag);
                addbyte(~TAG_UINT64);
                addbyte(0xdc); /*FADD ST[EAX*8]*/
                addbyte(0x04 | op);
                addbyte(0xc6);
                addbyte(0xdd); /*FSTP ST[EBX*8]*/
                addbyte(0x1c);
                addbyte(0xde);
        }
        else
        {
                addbyte(0xdd); /*FLD [ESI+EAX*8]*/
                addbyte(0x04);
                addbyte(0xc6);
                addbyte(0x80); /*AND tag[EAX], ~TAG_UINT64*/
                addbyte(0xa0);
                addlong((uintptr_t)tag);
                addbyte(~TAG_UINT64);
                addbyte(0xdc); /*FADD ST[EBX*8]*/
                addbyte(0x04 | op);
                addbyte(0xde);
                addbyte(0xdd); /*FSTP ST[EAX*8]*/
                addbyte(0x1c);
                addbyte(0xc6);
        }
}

static void FP_COMPARE_REG(int dst, int src)
{
        addbyte(0xa1); /*MOV EAX, TOP*/
        addlong((uintptr_t)&TOP);
        addbyte(0xbe); /*MOVL ESI, ST*/
        addlong((uintptr_t)ST);
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
        addbyte(0x0d);
        addlong(((uintptr_t)&npxs) + 1);
        addbyte(0xdb); /*FCLEX*/
        addbyte(0xe2);
        addbyte(0x80); /*AND CL, ~(C0|C2|C3)*/
        addbyte(0xe1);
        addbyte((~(C0|C2|C3)) >> 8);

        if (src)
        {
                addbyte(0xdd); /*FLD ST[EBX*8]*/
                addbyte(0x04);
                addbyte(0xde);
                addbyte(0xdc); /*FCOMP ST[EAX*8]*/
                addbyte(0x04 | 0x18);
                addbyte(0xc6);
        }
        else
        {
                addbyte(0xdd); /*FLD [ESI+EAX*8]*/
                addbyte(0x04);
                addbyte(0xc6);
                addbyte(0xdc); /*FCOMP ST[EBX*8]*/
                addbyte(0x04 | 0x18);
                addbyte(0xde);
        }

        addbyte(0xdf); /*FSTSW AX*/
        addbyte(0xe0);
        addbyte(0x80); /*AND AH, (C0|C2|C3)*/
        addbyte(0xe4);
        addbyte((C0|C2|C3) >> 8);
        addbyte(0x08); /*OR CL, AH*/
        addbyte(0xe1);
        addbyte(0x88); /*MOV [npxs+1], CL*/
        addbyte(0x0d);
        addlong(((uintptr_t)&npxs) + 1);
}

static int ZERO_EXTEND_W_B(int reg)
{
        addbyte(0x0f); /*MOVZX regl, regb*/
        addbyte(0xb6);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}
static int ZERO_EXTEND_L_B(int reg)
{
        addbyte(0x0f); /*MOVZX regl, regb*/
        addbyte(0xb6);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}
static int ZERO_EXTEND_L_W(int reg)
{
        addbyte(0x0f); /*MOVZX regl, regw*/
        addbyte(0xb7);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}

static int SIGN_EXTEND_W_B(int reg)
{
        addbyte(0x0f); /*MOVSX regl, regb*/
        addbyte(0xbe);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}
static int SIGN_EXTEND_L_B(int reg)
{
        addbyte(0x0f); /*MOVSX regl, regb*/
        addbyte(0xbe);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}
static int SIGN_EXTEND_L_W(int reg)
{
        addbyte(0x0f); /*MOVSX regl, regw*/
        addbyte(0xbf);
        addbyte(0xc0 | reg | (reg << 3));
        return reg;
}

static int COPY_REG(int src_reg)
{
        return src_reg;
}

static void SET_BITS(uintptr_t addr, uint32_t val)
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
static void CLEAR_BITS(uintptr_t addr, uint32_t val)
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

static void MMX_ENTER()
{
        if (codegen_mmx_entered)
                return;
                
        addbyte(0xf6); /*TEST cr0, 0xc*/
        addbyte(0x05);
        addlong((uintptr_t)&cr0);
        addbyte(0xc);
        addbyte(0x74); /*JZ +*/
        addbyte(10+7+5+5);
        addbyte(0xC7); /*MOVL [oldpc],op_old_pc*/
        addbyte(0x05);
        addlong((uintptr_t)&oldpc);
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
        addbyte(0xc7); /*MOV ISMMX, 1*/
        addbyte(0x05);
        addlong((uint32_t)&ismmx);
        addlong(1);
        addbyte(0xa3); /*MOV TOP, EAX*/
        addlong((uint32_t)&TOP);
        addbyte(0xa3); /*MOV tag, EAX*/      
        addlong((uint32_t)&tag[0]);
        addbyte(0xa3); /*MOV tag+4, EAX*/
        addlong((uint32_t)&tag[4]);

        codegen_mmx_entered = 1;
}

extern int mmx_ebx_ecx_loaded;

static int LOAD_MMX_D(int guest_reg)
{
        int host_reg = find_host_reg();
        host_reg_mapping[host_reg] = 100;

        addbyte(0x8b); /*MOV EBX, reg*/
        addbyte(0x05 | (host_reg << 3));
        addlong((uint32_t)&MM[guest_reg].l[0]);
        
        return host_reg;
}
static int LOAD_MMX_Q(int guest_reg, int *host_reg1, int *host_reg2)
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
        addbyte(0x05 | ((*host_reg1) << 3));
        addlong((uint32_t)&MM[guest_reg].l[0]);
        addbyte(0x8b); /*MOV ECX, reg+4*/
        addbyte(0x05 | ((*host_reg2) << 3));
        addlong((uint32_t)&MM[guest_reg].l[1]);
}
static int LOAD_MMX_Q_MMX(int guest_reg)
{
        int dst_reg = find_host_xmm_reg();
        host_reg_xmm_mapping[dst_reg] = guest_reg;

        addbyte(0xf3); /*MOVQ dst_reg,[reg]*/
        addbyte(0x0f);
        addbyte(0x7e);
        addbyte(0x05 | (dst_reg << 3));
        addlong((uint32_t)&MM[guest_reg].q);
        
        return dst_reg;
}

static int LOAD_INT_TO_MMX(int src_reg1, int src_reg2)
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

static void STORE_MMX_LQ(int guest_reg, int host_reg1)
{
        addbyte(0xC7); /*MOVL [reg],0*/
        addbyte(0x05);
        addlong((uint32_t)&MM[guest_reg].l[1]);
        addlong(0);
        addbyte(0x89); /*MOVL [reg],host_reg*/
        addbyte(0x05 | (host_reg1 << 3));
        addlong((uint32_t)&MM[guest_reg].l[0]);
}
static void STORE_MMX_Q(int guest_reg, int host_reg1, int host_reg2)
{
        addbyte(0x89); /*MOVL [reg],host_reg*/
        addbyte(0x05 | (host_reg1 << 3));
        addlong((uint32_t)&MM[guest_reg].l[0]);
        addbyte(0x89); /*MOVL [reg],host_reg*/
        addbyte(0x05 | (host_reg2 << 3));
        addlong((uint32_t)&MM[guest_reg].l[1]);
}
static void STORE_MMX_Q_MMX(int guest_reg, int host_reg)
{
        addbyte(0x66); /*MOVQ [guest_reg],host_reg*/
        addbyte(0x0f);
        addbyte(0xd6);
        addbyte(0x05 | (host_reg << 3));
        addlong((uint32_t)&MM[guest_reg].q);
}

#define MMX_x86_OP(name, opcode)                            \
static void MMX_ ## name(int dst_reg, int src_reg)      \
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

static void MMX_PACKSSWB(int dst_reg, int src_reg)
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
static void MMX_PACKUSWB(int dst_reg, int src_reg)
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
static void MMX_PACKSSDW(int dst_reg, int src_reg)
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
static void MMX_PUNPCKHBW(int dst_reg, int src_reg)
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
static void MMX_PUNPCKHWD(int dst_reg, int src_reg)
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
static void MMX_PUNPCKHDQ(int dst_reg, int src_reg)
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

static void MMX_PSRLW_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRLW dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x71);
        addbyte(0xc0 | dst_reg | 0x10);
        addbyte(amount);
}
static void MMX_PSRAW_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRAW dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x71);
        addbyte(0xc0 | dst_reg | 0x20);
        addbyte(amount);
}
static void MMX_PSLLW_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSLLW dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x71);
        addbyte(0xc0 | dst_reg | 0x30);
        addbyte(amount);
}

static void MMX_PSRLD_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRLD dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x72);
        addbyte(0xc0 | dst_reg | 0x10);
        addbyte(amount);
}
static void MMX_PSRAD_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRAD dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x72);
        addbyte(0xc0 | dst_reg | 0x20);
        addbyte(amount);
}
static void MMX_PSLLD_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSLLD dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x72);
        addbyte(0xc0 | dst_reg | 0x30);
        addbyte(amount);
}

static void MMX_PSRLQ_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRLQ dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x73);
        addbyte(0xc0 | dst_reg | 0x10);
        addbyte(amount);
}
static void MMX_PSRAQ_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSRAQ dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x73);
        addbyte(0xc0 | dst_reg | 0x20);
        addbyte(amount);
}
static void MMX_PSLLQ_imm(int dst_reg, int amount)
{
        addbyte(0x66); /*PSLLQ dst_reg, amount*/
        addbyte(0x0f);
        addbyte(0x73);
        addbyte(0xc0 | dst_reg | 0x30);
        addbyte(amount);
}
