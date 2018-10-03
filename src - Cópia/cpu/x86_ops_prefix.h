#define op_seg(name, seg, opcode_table, normal_opcode_table)    \
static int op ## name ## _w_a16(uint32_t fetchdat)              \
{                                                               \
        fetchdat = fastreadl(cs + cpu_state.pc);                \
        if (cpu_state.abrt) return 1;                                     \
        cpu_state.pc++;                                         \
                                                                \
        cpu_state.ea_seg = &seg;                                          \
        cpu_state.ssegs = 1;                                              \
        CLOCK_CYCLES(4);                                        \
        PREFETCH_PREFIX();                                      \
                                                                \
        if (opcode_table[fetchdat & 0xff])                      \
                return opcode_table[fetchdat & 0xff](fetchdat >> 8);    \
        return normal_opcode_table[fetchdat & 0xff](fetchdat >> 8); \
}                                                               \
                                                                \
static int op ## name ## _l_a16(uint32_t fetchdat)              \
{                                                               \
        fetchdat = fastreadl(cs + cpu_state.pc);                \
        if (cpu_state.abrt) return 1;                                     \
        cpu_state.pc++;                                         \
                                                                \
        cpu_state.ea_seg = &seg;                                          \
        cpu_state.ssegs = 1;                                              \
        CLOCK_CYCLES(4);                                        \
        PREFETCH_PREFIX();                                      \
                                                                \
        if (opcode_table[(fetchdat & 0xff) | 0x100])            \
                return opcode_table[(fetchdat & 0xff) | 0x100](fetchdat >> 8);      \
        return normal_opcode_table[(fetchdat & 0xff) | 0x100](fetchdat >> 8); \
}                                                               \
                                                                \
static int op ## name ## _w_a32(uint32_t fetchdat)              \
{                                                               \
        fetchdat = fastreadl(cs + cpu_state.pc);                \
        if (cpu_state.abrt) return 1;                                     \
        cpu_state.pc++;                                         \
                                                                \
        cpu_state.ea_seg = &seg;                                          \
        cpu_state.ssegs = 1;                                              \
        CLOCK_CYCLES(4);                                        \
        PREFETCH_PREFIX();                                      \
                                                                \
        if (opcode_table[(fetchdat & 0xff) | 0x200])            \
                return opcode_table[(fetchdat & 0xff) | 0x200](fetchdat >> 8);      \
        return normal_opcode_table[(fetchdat & 0xff) | 0x200](fetchdat >> 8); \
}                                                               \
                                                                \
static int op ## name ## _l_a32(uint32_t fetchdat)              \
{                                                               \
        fetchdat = fastreadl(cs + cpu_state.pc);                \
        if (cpu_state.abrt) return 1;                                     \
        cpu_state.pc++;                                         \
                                                                \
        cpu_state.ea_seg = &seg;                                          \
        cpu_state.ssegs = 1;                                              \
        CLOCK_CYCLES(4);                                        \
        PREFETCH_PREFIX();                                      \
                                                                \
        if (opcode_table[(fetchdat & 0xff) | 0x300])            \
                return opcode_table[(fetchdat & 0xff) | 0x300](fetchdat >> 8);      \
        return normal_opcode_table[(fetchdat & 0xff) | 0x300](fetchdat >> 8); \
}

op_seg(CS, _cs, x86_opcodes, x86_opcodes)
op_seg(DS, _ds, x86_opcodes, x86_opcodes)
op_seg(ES, _es, x86_opcodes, x86_opcodes)
op_seg(FS, _fs, x86_opcodes, x86_opcodes)
op_seg(GS, _gs, x86_opcodes, x86_opcodes)
op_seg(SS, _ss, x86_opcodes, x86_opcodes)

op_seg(CS_REPE, _cs, x86_opcodes_REPE, x86_opcodes)
op_seg(DS_REPE, _ds, x86_opcodes_REPE, x86_opcodes)
op_seg(ES_REPE, _es, x86_opcodes_REPE, x86_opcodes)
op_seg(FS_REPE, _fs, x86_opcodes_REPE, x86_opcodes)
op_seg(GS_REPE, _gs, x86_opcodes_REPE, x86_opcodes)
op_seg(SS_REPE, _ss, x86_opcodes_REPE, x86_opcodes)

op_seg(CS_REPNE, _cs, x86_opcodes_REPNE, x86_opcodes)
op_seg(DS_REPNE, _ds, x86_opcodes_REPNE, x86_opcodes)
op_seg(ES_REPNE, _es, x86_opcodes_REPNE, x86_opcodes)
op_seg(FS_REPNE, _fs, x86_opcodes_REPNE, x86_opcodes)
op_seg(GS_REPNE, _gs, x86_opcodes_REPNE, x86_opcodes)
op_seg(SS_REPNE, _ss, x86_opcodes_REPNE, x86_opcodes)

static int op_66(uint32_t fetchdat) /*Data size select*/
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        cpu_state.op32 = ((use32 & 0x100) ^ 0x100) | (cpu_state.op32 & 0x200);
        CLOCK_CYCLES(2);
        PREFETCH_PREFIX();
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
}
static int op_67(uint32_t fetchdat) /*Address size select*/
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        cpu_state.op32 = ((use32 & 0x200) ^ 0x200) | (cpu_state.op32 & 0x100);
        CLOCK_CYCLES(2);
        PREFETCH_PREFIX();
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
}

static int op_66_REPE(uint32_t fetchdat) /*Data size select*/
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        cpu_state.op32 = ((use32 & 0x100) ^ 0x100) | (cpu_state.op32 & 0x200);
        CLOCK_CYCLES(2);
        PREFETCH_PREFIX();
        if (x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32])
                return x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);        
}
static int op_67_REPE(uint32_t fetchdat) /*Address size select*/
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        cpu_state.op32 = ((use32 & 0x200) ^ 0x200) | (cpu_state.op32 & 0x100);
        CLOCK_CYCLES(2);
        PREFETCH_PREFIX();
        if (x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32])
                return x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);        
}
static int op_66_REPNE(uint32_t fetchdat) /*Data size select*/
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        cpu_state.op32 = ((use32 & 0x100) ^ 0x100) | (cpu_state.op32 & 0x200);
        CLOCK_CYCLES(2);
        PREFETCH_PREFIX();
        if (x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32])
                return x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);        
}
static int op_67_REPNE(uint32_t fetchdat) /*Address size select*/
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        cpu_state.op32 = ((use32 & 0x200) ^ 0x200) | (cpu_state.op32 & 0x100);
        CLOCK_CYCLES(2);
        PREFETCH_PREFIX();
        if (x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32])
                return x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);        
}
