/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#define op_seg(name, seg)                                       \
static int op ## name ## _w_a16(uint32_t fetchdat)              \
{                                                               \
        fetchdat = fastreadl(cs + cpu_state.pc);                \
        if (abrt) return 1;                                     \
        cpu_state.pc++;                                         \
                                                                \
        ea_seg = &seg;                                          \
        ssegs = 1;                                              \
        CLOCK_CYCLES(4);                                        \
                                                                \
        return x86_opcodes[fetchdat & 0xff](fetchdat >> 8);     \
}                                                               \
                                                                \
static int op ## name ## _l_a16(uint32_t fetchdat)              \
{                                                               \
        fetchdat = fastreadl(cs + cpu_state.pc);                \
        if (abrt) return 1;                                     \
        cpu_state.pc++;                                         \
                                                                \
        ea_seg = &seg;                                          \
        ssegs = 1;                                              \
        CLOCK_CYCLES(4);                                        \
                                                                \
        return x86_opcodes[(fetchdat & 0xff) | 0x100](fetchdat >> 8);      \
}                                                               \
                                                                \
static int op ## name ## _w_a32(uint32_t fetchdat)              \
{                                                               \
        fetchdat = fastreadl(cs + cpu_state.pc);                \
        if (abrt) return 1;                                     \
        cpu_state.pc++;                                         \
                                                                \
        ea_seg = &seg;                                          \
        ssegs = 1;                                              \
        CLOCK_CYCLES(4);                                        \
                                                                \
        return x86_opcodes[(fetchdat & 0xff) | 0x200](fetchdat >> 8);      \
}                                                               \
                                                                \
static int op ## name ## _l_a32(uint32_t fetchdat)              \
{                                                               \
        fetchdat = fastreadl(cs + cpu_state.pc);                \
        if (abrt) return 1;                                     \
        cpu_state.pc++;                                         \
                                                                \
        ea_seg = &seg;                                          \
        ssegs = 1;                                              \
        CLOCK_CYCLES(4);                                        \
                                                                \
        return x86_opcodes[(fetchdat & 0xff) | 0x300](fetchdat >> 8);      \
}

op_seg(CS, _cs)
op_seg(DS, _ds)
op_seg(ES, _es)
op_seg(FS, _fs)
op_seg(GS, _gs)
op_seg(SS, _ss)

static int op_66(uint32_t fetchdat) /*Data size select*/
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (abrt) return 1;
        cpu_state.pc++;

        op32 = ((use32 & 0x100) ^ 0x100) | (op32 & 0x200);
        CLOCK_CYCLES(2);
        return x86_opcodes[(fetchdat & 0xff) | op32](fetchdat >> 8);
}
static int op_67(uint32_t fetchdat) /*Address size select*/
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (abrt) return 1;
        cpu_state.pc++;

        op32 = ((use32 & 0x200) ^ 0x200) | (op32 & 0x100);
        CLOCK_CYCLES(2);
        return x86_opcodes[(fetchdat & 0xff) | op32](fetchdat >> 8);
}
