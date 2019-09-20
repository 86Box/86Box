extern int tempc;

enum
{
        FLAGS_UNKNOWN,
        
        FLAGS_ZN8,
        FLAGS_ZN16,
        FLAGS_ZN32,
        
        FLAGS_ADD8,
        FLAGS_ADD16,
        FLAGS_ADD32,
        
        FLAGS_SUB8,
        FLAGS_SUB16,
        FLAGS_SUB32,
        
        FLAGS_SHL8,
        FLAGS_SHL16,
        FLAGS_SHL32,

        FLAGS_SHR8,
        FLAGS_SHR16,
        FLAGS_SHR32,

        FLAGS_SAR8,
        FLAGS_SAR16,
        FLAGS_SAR32,

        FLAGS_ROL8,
        FLAGS_ROL16,
        FLAGS_ROL32,

        FLAGS_ROR8,
        FLAGS_ROR16,
        FLAGS_ROR32,

        FLAGS_INC8,
        FLAGS_INC16,
        FLAGS_INC32,
        
        FLAGS_DEC8,
        FLAGS_DEC16,
        FLAGS_DEC32,

        FLAGS_ADC8,
        FLAGS_ADC16,
        FLAGS_ADC32,

        FLAGS_SBC8,
        FLAGS_SBC16,
        FLAGS_SBC32
};

static inline int ZF_SET()
{
        switch (cpu_state.flags_op)
        {
                case FLAGS_ZN8: 
                case FLAGS_ZN16:
                case FLAGS_ZN32:
                case FLAGS_ADD8:
                case FLAGS_ADD16:
                case FLAGS_ADD32:
                case FLAGS_SUB8:
                case FLAGS_SUB16:
                case FLAGS_SUB32:
                case FLAGS_SHL8:
                case FLAGS_SHL16:
                case FLAGS_SHL32:
                case FLAGS_SHR8:
                case FLAGS_SHR16:
                case FLAGS_SHR32:
                case FLAGS_SAR8:
                case FLAGS_SAR16:
                case FLAGS_SAR32:
                case FLAGS_INC8:
                case FLAGS_INC16:
                case FLAGS_INC32:
                case FLAGS_DEC8:
                case FLAGS_DEC16:
                case FLAGS_DEC32:
                case FLAGS_ADC8:
                case FLAGS_ADC16:
                case FLAGS_ADC32:
                case FLAGS_SBC8:
                case FLAGS_SBC16:
                case FLAGS_SBC32:
                return !cpu_state.flags_res;
                
                case FLAGS_ROL8:
                case FLAGS_ROL16:
                case FLAGS_ROL32:
                case FLAGS_ROR8:
                case FLAGS_ROR16:
                case FLAGS_ROR32:
                case FLAGS_UNKNOWN:
                return cpu_state.flags & Z_FLAG;
        }
        return 0;
}

static inline int NF_SET()
{
        switch (cpu_state.flags_op)
        {
                case FLAGS_ZN8: 
                case FLAGS_ADD8:
                case FLAGS_SUB8:
                case FLAGS_SHL8:
                case FLAGS_SHR8:
                case FLAGS_SAR8:
                case FLAGS_INC8:
                case FLAGS_DEC8:
                case FLAGS_ADC8:
                case FLAGS_SBC8:
                return cpu_state.flags_res & 0x80;
                
                case FLAGS_ZN16:
                case FLAGS_ADD16:
                case FLAGS_SUB16:
                case FLAGS_SHL16:
                case FLAGS_SHR16:
                case FLAGS_SAR16:
                case FLAGS_INC16:
                case FLAGS_DEC16:
                case FLAGS_ADC16:
                case FLAGS_SBC16:
                return cpu_state.flags_res & 0x8000;
                
                case FLAGS_ZN32:
                case FLAGS_ADD32:
                case FLAGS_SUB32:
                case FLAGS_SHL32:
                case FLAGS_SHR32:
                case FLAGS_SAR32:
                case FLAGS_INC32:
                case FLAGS_DEC32:
                case FLAGS_ADC32:
                case FLAGS_SBC32:
                return cpu_state.flags_res & 0x80000000;
                
                case FLAGS_ROL8:
                case FLAGS_ROL16:
                case FLAGS_ROL32:
                case FLAGS_ROR8:
                case FLAGS_ROR16:
                case FLAGS_ROR32:
                case FLAGS_UNKNOWN:
                return cpu_state.flags & N_FLAG;
        }
        return 0;
}

static inline int PF_SET()
{
        switch (cpu_state.flags_op)
        {
                case FLAGS_ZN8: 
                case FLAGS_ZN16:
                case FLAGS_ZN32:
                case FLAGS_ADD8:
                case FLAGS_ADD16:
                case FLAGS_ADD32:
                case FLAGS_SUB8:
                case FLAGS_SUB16:
                case FLAGS_SUB32:
                case FLAGS_SHL8:
                case FLAGS_SHL16:
                case FLAGS_SHL32:
                case FLAGS_SHR8:
                case FLAGS_SHR16:
                case FLAGS_SHR32:
                case FLAGS_SAR8:
                case FLAGS_SAR16:
                case FLAGS_SAR32:
                case FLAGS_INC8:
                case FLAGS_INC16:
                case FLAGS_INC32:
                case FLAGS_DEC8:
                case FLAGS_DEC16:
                case FLAGS_DEC32:
                case FLAGS_ADC8:
                case FLAGS_ADC16:
                case FLAGS_ADC32:
                case FLAGS_SBC8:
                case FLAGS_SBC16:
                case FLAGS_SBC32:
                return znptable8[cpu_state.flags_res & 0xff] & P_FLAG;
                
                case FLAGS_ROL8:
                case FLAGS_ROL16:
                case FLAGS_ROL32:
                case FLAGS_ROR8:
                case FLAGS_ROR16:
                case FLAGS_ROR32:
                case FLAGS_UNKNOWN:
                return cpu_state.flags & P_FLAG;
        }
        return 0;
}

static inline int VF_SET()
{
        switch (cpu_state.flags_op)
        {
                case FLAGS_ZN8:
                case FLAGS_ZN16:
                case FLAGS_ZN32:
                case FLAGS_SAR8:
                case FLAGS_SAR16:
                case FLAGS_SAR32:
                return 0;
                
                case FLAGS_ADC8:
                case FLAGS_ADD8:
                case FLAGS_INC8:
                return !((cpu_state.flags_op1 ^ cpu_state.flags_op2) & 0x80) && ((cpu_state.flags_op1 ^ cpu_state.flags_res) & 0x80);
                case FLAGS_ADC16:
                case FLAGS_ADD16:
                case FLAGS_INC16:
                return !((cpu_state.flags_op1 ^ cpu_state.flags_op2) & 0x8000) && ((cpu_state.flags_op1 ^ cpu_state.flags_res) & 0x8000);
                case FLAGS_ADC32:
                case FLAGS_ADD32:
                case FLAGS_INC32:
                return !((cpu_state.flags_op1 ^ cpu_state.flags_op2) & 0x80000000) && ((cpu_state.flags_op1 ^ cpu_state.flags_res) & 0x80000000);
                                
                case FLAGS_SBC8:
                case FLAGS_SUB8:
                case FLAGS_DEC8:
                return ((cpu_state.flags_op1 ^ cpu_state.flags_op2) & (cpu_state.flags_op1 ^ cpu_state.flags_res) & 0x80);
                case FLAGS_SBC16:
                case FLAGS_SUB16:
                case FLAGS_DEC16:
                return ((cpu_state.flags_op1 ^ cpu_state.flags_op2) & (cpu_state.flags_op1 ^ cpu_state.flags_res) & 0x8000);
                case FLAGS_SBC32:
                case FLAGS_SUB32:
                case FLAGS_DEC32:
                return ((cpu_state.flags_op1 ^ cpu_state.flags_op2) & (cpu_state.flags_op1 ^ cpu_state.flags_res) & 0x80000000);

                case FLAGS_SHL8:
                return (((cpu_state.flags_op1 << cpu_state.flags_op2) ^ (cpu_state.flags_op1 << (cpu_state.flags_op2 - 1))) & 0x80);
                case FLAGS_SHL16:
                return (((cpu_state.flags_op1 << cpu_state.flags_op2) ^ (cpu_state.flags_op1 << (cpu_state.flags_op2 - 1))) & 0x8000);
                case FLAGS_SHL32:
                return (((cpu_state.flags_op1 << cpu_state.flags_op2) ^ (cpu_state.flags_op1 << (cpu_state.flags_op2 - 1))) & 0x80000000);
                
                case FLAGS_SHR8:
                return ((cpu_state.flags_op2 == 1) && (cpu_state.flags_op1 & 0x80));
                case FLAGS_SHR16:
                return ((cpu_state.flags_op2 == 1) && (cpu_state.flags_op1 & 0x8000));
                case FLAGS_SHR32:
                return ((cpu_state.flags_op2 == 1) && (cpu_state.flags_op1 & 0x80000000));

                case FLAGS_ROL8:
                return (cpu_state.flags_res ^ (cpu_state.flags_res >> 7)) & 1;
                case FLAGS_ROL16:
                return (cpu_state.flags_res ^ (cpu_state.flags_res >> 15)) & 1;
                case FLAGS_ROL32:
                return (cpu_state.flags_res ^ (cpu_state.flags_res >> 31)) & 1;

                case FLAGS_ROR8:
                return (cpu_state.flags_res ^ (cpu_state.flags_res >> 1)) & 0x40;
                case FLAGS_ROR16:
                return (cpu_state.flags_res ^ (cpu_state.flags_res >> 1)) & 0x4000;
                case FLAGS_ROR32:
                return (cpu_state.flags_res ^ (cpu_state.flags_res >> 1)) & 0x40000000;

                case FLAGS_UNKNOWN:
                return cpu_state.flags & V_FLAG;
        }
        return 0;
}

static inline int AF_SET()
{
        switch (cpu_state.flags_op)
        {
                case FLAGS_ZN8: 
                case FLAGS_ZN16:
                case FLAGS_ZN32:
                case FLAGS_SHL8:
                case FLAGS_SHL16:
                case FLAGS_SHL32:
                case FLAGS_SHR8:
                case FLAGS_SHR16:
                case FLAGS_SHR32:
                case FLAGS_SAR8:
                case FLAGS_SAR16:
                case FLAGS_SAR32:
                return 0;
                
                case FLAGS_ADD8:
                case FLAGS_ADD16:
                case FLAGS_ADD32:
                case FLAGS_INC8:
                case FLAGS_INC16:
                case FLAGS_INC32:
                return ((cpu_state.flags_op1 & 0xF) + (cpu_state.flags_op2 & 0xF)) & 0x10;

                case FLAGS_ADC8:
                return ((cpu_state.flags_res & 0xf) < (cpu_state.flags_op1 & 0xf)) ||
                        ((cpu_state.flags_res & 0xf) == (cpu_state.flags_op1 & 0xf) && cpu_state.flags_op2 == 0xff);
                case FLAGS_ADC16:
                return ((cpu_state.flags_res & 0xf) < (cpu_state.flags_op1 & 0xf)) ||
                        ((cpu_state.flags_res & 0xf) == (cpu_state.flags_op1 & 0xf) && cpu_state.flags_op2 == 0xffff);
                case FLAGS_ADC32:
                return ((cpu_state.flags_res & 0xf) < (cpu_state.flags_op1 & 0xf)) ||
                        ((cpu_state.flags_res & 0xf) == (cpu_state.flags_op1 & 0xf) && cpu_state.flags_op2 == 0xffffffff);

                case FLAGS_SUB8:
                case FLAGS_SUB16:
                case FLAGS_SUB32:
                case FLAGS_DEC8:
                case FLAGS_DEC16:
                case FLAGS_DEC32:
                return ((cpu_state.flags_op1 & 0xF) - (cpu_state.flags_op2 & 0xF)) & 0x10;

                case FLAGS_SBC8:
                case FLAGS_SBC16:
                case FLAGS_SBC32:
                return ((cpu_state.flags_op1 & 0xf) < (cpu_state.flags_op2 & 0xf)) ||
                        ((cpu_state.flags_op1 & 0xf) == (cpu_state.flags_op2 & 0xf) && (cpu_state.flags_res & 0xf) != 0);
                
                case FLAGS_ROL8:
                case FLAGS_ROL16:
                case FLAGS_ROL32:
                case FLAGS_ROR8:
                case FLAGS_ROR16:
                case FLAGS_ROR32:
                case FLAGS_UNKNOWN:
                return cpu_state.flags & A_FLAG;
        }
        return 0;
}

static inline int CF_SET()
{
        switch (cpu_state.flags_op)
        {
                case FLAGS_ADD8:
                return ((cpu_state.flags_op1 + cpu_state.flags_op2) & 0x100) ? 1 : 0;
                case FLAGS_ADD16:
                return ((cpu_state.flags_op1 + cpu_state.flags_op2) & 0x10000) ? 1 : 0;
                case FLAGS_ADD32:
                return (cpu_state.flags_res < cpu_state.flags_op1);

                case FLAGS_ADC8:
                return (cpu_state.flags_res < cpu_state.flags_op1) ||
                        (cpu_state.flags_res == cpu_state.flags_op1 && cpu_state.flags_op2 == 0xff);
                case FLAGS_ADC16:
                return (cpu_state.flags_res < cpu_state.flags_op1) ||
                        (cpu_state.flags_res == cpu_state.flags_op1 && cpu_state.flags_op2 == 0xffff);
                case FLAGS_ADC32:
                return (cpu_state.flags_res < cpu_state.flags_op1) ||
                        (cpu_state.flags_res == cpu_state.flags_op1 && cpu_state.flags_op2 == 0xffffffff);

                case FLAGS_SUB8:
                case FLAGS_SUB16:
                case FLAGS_SUB32:
                return (cpu_state.flags_op1 < cpu_state.flags_op2);

                case FLAGS_SBC8:
                case FLAGS_SBC16:
                case FLAGS_SBC32:
                return (cpu_state.flags_op1 < cpu_state.flags_op2) ||
                        (cpu_state.flags_op1 == cpu_state.flags_op2 && cpu_state.flags_res != 0);

                case FLAGS_SHL8:
                return ((cpu_state.flags_op1 << (cpu_state.flags_op2 - 1)) & 0x80) ? 1 : 0;
                case FLAGS_SHL16:
                return ((cpu_state.flags_op1 << (cpu_state.flags_op2 - 1)) & 0x8000) ? 1 : 0;
                case FLAGS_SHL32:
                return ((cpu_state.flags_op1 << (cpu_state.flags_op2 - 1)) & 0x80000000) ? 1 : 0;

                case FLAGS_SHR8:
                case FLAGS_SHR16:
                case FLAGS_SHR32:
                return (cpu_state.flags_op1 >> (cpu_state.flags_op2 - 1)) & 1;

                case FLAGS_SAR8:
                return ((int8_t)cpu_state.flags_op1 >> (cpu_state.flags_op2 - 1)) & 1;
                case FLAGS_SAR16:
                return ((int16_t)cpu_state.flags_op1 >> (cpu_state.flags_op2 - 1)) & 1;
                case FLAGS_SAR32:
                return ((int32_t)cpu_state.flags_op1 >> (cpu_state.flags_op2 - 1)) & 1;

                case FLAGS_ZN8: 
                case FLAGS_ZN16:
                case FLAGS_ZN32:
                return 0;

                case FLAGS_ROL8:
                case FLAGS_ROL16:
                case FLAGS_ROL32:
                return cpu_state.flags_res & 1;
                
                case FLAGS_ROR8:
                return (cpu_state.flags_res & 0x80) ? 1 : 0;
                case FLAGS_ROR16:
                return (cpu_state.flags_res & 0x8000) ? 1 :0;
                case FLAGS_ROR32:
                return (cpu_state.flags_res & 0x80000000) ? 1 : 0;
                
                case FLAGS_DEC8:
                case FLAGS_DEC16:
                case FLAGS_DEC32:
                case FLAGS_INC8:
                case FLAGS_INC16:
                case FLAGS_INC32:
                case FLAGS_UNKNOWN:
                return cpu_state.flags & C_FLAG;
        }
        return 0;
}

//#define ZF_SET() (flags & Z_FLAG)
//#define NF_SET() (flags & N_FLAG)
//#define PF_SET() (flags & P_FLAG)
//#define VF_SET() (flags & V_FLAG)
//#define CF_SET() (flags & C_FLAG)
//#define AF_SET() (flags & A_FLAG)

static inline void flags_rebuild()
{
        if (cpu_state.flags_op != FLAGS_UNKNOWN)
        {
                uint16_t tempf = 0;
                if (CF_SET()) tempf |= C_FLAG;
                if (PF_SET()) tempf |= P_FLAG;
                if (AF_SET()) tempf |= A_FLAG;
                if (ZF_SET()) tempf |= Z_FLAG;                                
                if (NF_SET()) tempf |= N_FLAG;
                if (VF_SET()) tempf |= V_FLAG;
                cpu_state.flags = (cpu_state.flags & ~0x8d5) | tempf;
                cpu_state.flags_op = FLAGS_UNKNOWN;
        }
}

static inline void flags_extract()
{
        cpu_state.flags_op = FLAGS_UNKNOWN;
}

static inline void flags_rebuild_c()
{
        if (cpu_state.flags_op != FLAGS_UNKNOWN)
        {
                if (CF_SET())
                        cpu_state.flags |=  C_FLAG;
                else
                        cpu_state.flags &= ~C_FLAG;
        }                
}

static inline int flags_res_valid()
{
        if (cpu_state.flags_op == FLAGS_UNKNOWN ||
            (cpu_state.flags_op >= FLAGS_ROL8 && cpu_state.flags_op <= FLAGS_ROR32))
                return 0;

        return 1;
}

static inline void setznp8(uint8_t val)
{
        cpu_state.flags_op = FLAGS_ZN8;
        cpu_state.flags_res = val;
}
static inline void setznp16(uint16_t val)
{
        cpu_state.flags_op = FLAGS_ZN16;
        cpu_state.flags_res = val;
}
static inline void setznp32(uint32_t val)
{
        cpu_state.flags_op = FLAGS_ZN32;
        cpu_state.flags_res = val;
}

#define set_flags_shift(op, orig, shift, res) \
        cpu_state.flags_op = op;                  \
        cpu_state.flags_res = res;                \
        cpu_state.flags_op1 = orig;               \
        cpu_state.flags_op2 = shift;

#define set_flags_rotate(op, res)               \
        cpu_state.flags_op = op;                \
        cpu_state.flags_res = res;

static inline void setadd8(uint8_t a, uint8_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a + b) & 0xff;
        cpu_state.flags_op = FLAGS_ADD8;
}
static inline void setadd16(uint16_t a, uint16_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a + b) & 0xffff;
        cpu_state.flags_op = FLAGS_ADD16;
}
static inline void setadd32(uint32_t a, uint32_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = a + b;
        cpu_state.flags_op = FLAGS_ADD32;
}
static inline void setadd8nc(uint8_t a, uint8_t b)
{
        flags_rebuild_c();
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a + b) & 0xff;
        cpu_state.flags_op = FLAGS_INC8;
}
static inline void setadd16nc(uint16_t a, uint16_t b)
{
        flags_rebuild_c();
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a + b) & 0xffff;
        cpu_state.flags_op = FLAGS_INC16;
}
static inline void setadd32nc(uint32_t a, uint32_t b)
{
        flags_rebuild_c();
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = a + b;
        cpu_state.flags_op = FLAGS_INC32;
}

static inline void setsub8(uint8_t a, uint8_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a - b) & 0xff;
        cpu_state.flags_op = FLAGS_SUB8;
}
static inline void setsub16(uint16_t a, uint16_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a - b) & 0xffff;
        cpu_state.flags_op = FLAGS_SUB16;
}
static inline void setsub32(uint32_t a, uint32_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = a - b;
        cpu_state.flags_op = FLAGS_SUB32;
}

static inline void setsub8nc(uint8_t a, uint8_t b)
{
        flags_rebuild_c();
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a - b) & 0xff;
        cpu_state.flags_op = FLAGS_DEC8;
}
static inline void setsub16nc(uint16_t a, uint16_t b)
{
        flags_rebuild_c();
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a - b) & 0xffff;
        cpu_state.flags_op = FLAGS_DEC16;
}
static inline void setsub32nc(uint32_t a, uint32_t b)
{
        flags_rebuild_c();
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = a - b;
        cpu_state.flags_op = FLAGS_DEC32;
}

static inline void setadc8(uint8_t a, uint8_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a + b + tempc) & 0xff;
        cpu_state.flags_op = FLAGS_ADC8;
}
static inline void setadc16(uint16_t a, uint16_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a + b + tempc) & 0xffff;
        cpu_state.flags_op = FLAGS_ADC16;
}
static inline void setadc32(uint32_t a, uint32_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = a + b + tempc;
        cpu_state.flags_op = FLAGS_ADC32;
}

static inline void setsbc8(uint8_t a, uint8_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a - (b + tempc)) & 0xff;
        cpu_state.flags_op = FLAGS_SBC8;
}
static inline void setsbc16(uint16_t a, uint16_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = (a - (b + tempc)) & 0xffff;
        cpu_state.flags_op = FLAGS_SBC16;
}
static inline void setsbc32(uint32_t a, uint32_t b)
{
        cpu_state.flags_op1 = a;
        cpu_state.flags_op2 = b;
        cpu_state.flags_res = a - (b + tempc);
        cpu_state.flags_op = FLAGS_SBC32;
}

extern void cpu_386_flags_extract();
extern void cpu_386_flags_rebuild();
