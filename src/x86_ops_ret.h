/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#define RETF_a16(stack_offset)                                  \
                if ((msw&1) && !(eflags&VM_FLAG))               \
                {                                               \
                        pmoderetf(0, stack_offset);             \
                        return 1;                               \
                }                                               \
                oxpc = cpu_state.pc;                            \
                if (stack32)                                    \
                {                                               \
                        cpu_state.pc = readmemw(ss, ESP);       \
                        loadcs(readmemw(ss, ESP + 2));          \
                }                                               \
                else                                            \
                {                                               \
                        cpu_state.pc = readmemw(ss, SP);        \
                        loadcs(readmemw(ss, SP + 2));           \
                }                                               \
                if (abrt) return 1;                             \
                if (stack32) ESP += 4 + stack_offset;           \
                else         SP  += 4 + stack_offset;           \
                cycles -= timing_retf_rm;

#define RETF_a32(stack_offset)                                  \
                if ((msw&1) && !(eflags&VM_FLAG))               \
                {                                               \
                        pmoderetf(1, stack_offset);             \
                        return 1;                               \
                }                                               \
                oxpc = cpu_state.pc;                            \
                if (stack32)                                    \
                {                                               \
                        cpu_state.pc = readmeml(ss, ESP);       \
                        loadcs(readmeml(ss, ESP + 4) & 0xffff); \
                }                                               \
                else                                            \
                {                                               \
                        cpu_state.pc = readmeml(ss, SP);        \
                        loadcs(readmeml(ss, SP + 4) & 0xffff);  \
                }                                               \
                if (abrt) return 1;                             \
                if (stack32) ESP += 8 + stack_offset;           \
                else         SP  += 8 + stack_offset;           \
                cycles -= timing_retf_rm;

static int opRETF_a16(uint32_t fetchdat)
{
        CPU_BLOCK_END();
        RETF_a16(0);
        return 0;
}
static int opRETF_a32(uint32_t fetchdat)
{
        CPU_BLOCK_END();
        RETF_a32(0);
        return 0;
}

static int opRETF_a16_imm(uint32_t fetchdat)
{
        uint16_t offset = getwordf();
        CPU_BLOCK_END();
        RETF_a16(offset);
        return 0;
}
static int opRETF_a32_imm(uint32_t fetchdat)
{
        uint16_t offset = getwordf();
        CPU_BLOCK_END();
        RETF_a32(offset);
        return 0;
}

static int opIRET_286(uint32_t fetchdat)
{
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        if (msw&1)
        {
                optype = IRET;
                pmodeiret(0);
                optype = 0;
        }
        else
        {
                uint16_t new_cs;
                oxpc = cpu_state.pc;
                if (stack32)
                {
                        cpu_state.pc = readmemw(ss, ESP);
                        new_cs = readmemw(ss, ESP + 2);
                        flags = (flags & 0x7000) | (readmemw(ss, ESP + 4) & 0xffd5) | 2;
                        ESP += 6;
                }
                else
                {
                        cpu_state.pc = readmemw(ss, SP);
                        new_cs = readmemw(ss, ((SP + 2) & 0xffff));
                        flags = (flags & 0x7000) | (readmemw(ss, ((SP + 4) & 0xffff)) & 0x0fd5) | 2;
                        SP += 6;
                }
                loadcs(new_cs);
                cycles -= timing_iret_rm;
        }
        flags_extract();
        nmi_enable = 1;
        CPU_BLOCK_END();
        return abrt;
}

static int opIRET(uint32_t fetchdat)
{
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        if (msw&1)
        {
                optype = IRET;
                pmodeiret(0);
                optype = 0;
        }
        else
        {
                uint16_t new_cs;
                oxpc = cpu_state.pc;
                if (stack32)
                {
                        cpu_state.pc = readmemw(ss, ESP);
                        new_cs = readmemw(ss, ESP + 2);
                        flags = (readmemw(ss, ESP + 4) & 0xffd5) | 2;
                        ESP += 6;
                }
                else
                {
                        cpu_state.pc = readmemw(ss, SP);
                        new_cs = readmemw(ss, ((SP + 2) & 0xffff));
                        flags = (readmemw(ss, ((SP + 4) & 0xffff)) & 0xffd5) | 2;
                        SP += 6;
                }
                loadcs(new_cs);
                cycles -= timing_iret_rm;
        }
        flags_extract();
        nmi_enable = 1;
        CPU_BLOCK_END();
        return abrt;
}

static int opIRETD(uint32_t fetchdat)
{
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        if (msw & 1)
        {
                optype = IRET;
                pmodeiret(1);
                optype = 0;
        }
        else
        {
                uint16_t new_cs;
                oxpc = cpu_state.pc;
                if (stack32)
                {
                        cpu_state.pc = readmeml(ss, ESP);
                        new_cs = readmemw(ss, ESP + 4);
                        flags = (readmemw(ss, ESP + 8) & 0xffd5) | 2;
                        eflags = readmemw(ss, ESP + 10);
                        ESP += 12;
                }
                else
                {
                        cpu_state.pc = readmeml(ss, SP);
                        new_cs = readmemw(ss, ((SP + 4) & 0xffff));
                        flags = (readmemw(ss,(SP + 8) & 0xffff) & 0xffd5) | 2;
                        eflags = readmemw(ss, (SP + 10) & 0xffff);
                        SP += 12;
                }
                loadcs(new_cs);
                cycles -= timing_iret_rm;
        }
        flags_extract();
        nmi_enable = 1;
        CPU_BLOCK_END();
        return abrt;
}
 
