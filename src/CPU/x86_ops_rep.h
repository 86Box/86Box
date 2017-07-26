extern int trap;

#define REP_OPS(size, CNT_REG, SRC_REG, DEST_REG) \
static int opREP_INSB_ ## size(uint32_t fetchdat)                               \
{                                                                               \
        int reads = 0, writes = 0, total_cycles = 0;                            \
                                                                                \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint8_t temp;                                                   \
                                                                                \
                check_io_perm(DX);                                               \
                temp = inb(DX);                                                 \
                writememb(es, DEST_REG, temp); if (cpu_state.abrt) return 1;    \
                                                                                \
                if (flags & D_FLAG) DEST_REG--;                                 \
                else                DEST_REG++;                                 \
                CNT_REG--;                                                      \
                cycles -= 15;                                                   \
                reads++; writes++; total_cycles += 15;                          \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);              \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_INSW_ ## size(uint32_t fetchdat)                               \
{                                                                               \
        int reads = 0, writes = 0, total_cycles = 0;                            \
                                                                                \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint16_t temp;                                                  \
                                                                                \
                check_io_perm(DX);                                               \
                check_io_perm(DX+1);                                             \
                temp = inw(DX);                                                 \
                writememw(es, DEST_REG, temp); if (cpu_state.abrt) return 1;       \
                                                                                \
                if (flags & D_FLAG) DEST_REG -= 2;                              \
                else                DEST_REG += 2;                              \
                CNT_REG--;                                                      \
                cycles -= 15;                                                   \
                reads++; writes++; total_cycles += 15;                          \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);              \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_INSL_ ## size(uint32_t fetchdat)                               \
{                                                                               \
        int reads = 0, writes = 0, total_cycles = 0;                            \
                                                                                \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint32_t temp;                                                  \
                                                                                \
                check_io_perm(DX);                                               \
                check_io_perm(DX+1);                                             \
                check_io_perm(DX+2);                                             \
                check_io_perm(DX+3);                                             \
                temp = inl(DX);                                                 \
                writememl(es, DEST_REG, temp); if (cpu_state.abrt) return 1;       \
                                                                                \
                if (flags & D_FLAG) DEST_REG -= 4;                              \
                else                DEST_REG += 4;                              \
                CNT_REG--;                                                      \
                cycles -= 15;                                                   \
                reads++; writes++; total_cycles += 15;                          \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, 0, reads, 0, writes, 0);              \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
static int opREP_OUTSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, writes = 0, total_cycles = 0;                            \
                                                                                \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint8_t temp = readmemb(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;    \
                check_io_perm(DX);                                               \
                outb(DX, temp);                                                 \
                if (flags & D_FLAG) SRC_REG--;                                  \
                else                SRC_REG++;                                  \
                CNT_REG--;                                                      \
                cycles -= 14;                                                   \
                reads++; writes++; total_cycles += 14;                          \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);              \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_OUTSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, writes = 0, total_cycles = 0;                            \
                                                                                \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint16_t temp = readmemw(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;   \
                check_io_perm(DX);                                               \
                check_io_perm(DX+1);                                             \
                outw(DX, temp);                                                 \
                if (flags & D_FLAG) SRC_REG -= 2;                               \
                else                SRC_REG += 2;                               \
                CNT_REG--;                                                      \
                cycles -= 14;                                                   \
                reads++; writes++; total_cycles += 14;                          \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);              \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_OUTSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, writes = 0, total_cycles = 0;                            \
                                                                                \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint32_t temp = readmeml(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;   \
                check_io_perm(DX);                                               \
                check_io_perm(DX+1);                                             \
                check_io_perm(DX+2);                                             \
                check_io_perm(DX+3);                                             \
                outl(DX, temp);                                                 \
                if (flags & D_FLAG) SRC_REG -= 4;                               \
                else                SRC_REG += 4;                               \
                CNT_REG--;                                                      \
                cycles -= 14;                                                   \
                reads++; writes++; total_cycles += 14;                          \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, 0, reads, 0, writes, 0);              \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
static int opREP_MOVSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, writes = 0, total_cycles = 0;                            \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                uint8_t temp;                                                   \
                                                                                \
                CHECK_WRITE_REP(&_es, DEST_REG, DEST_REG);                      \
                temp = readmemb(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;    \
                writememb(es, DEST_REG, temp); if (cpu_state.abrt) return 1;       \
                                                                                \
                if (flags & D_FLAG) { DEST_REG--; SRC_REG--; }                  \
                else                { DEST_REG++; SRC_REG++; }                  \
                CNT_REG--;                                                      \
                cycles -= is486 ? 3 : 4;                                        \
                ins++;                                                          \
                reads++; writes++; total_cycles += is486 ? 3 : 4;               \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        ins--;                                                                  \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);              \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_MOVSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, writes = 0, total_cycles = 0;                            \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                uint16_t temp;                                                  \
                                                                                \
                CHECK_WRITE_REP(&_es, DEST_REG, DEST_REG);                      \
                temp = readmemw(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;    \
                writememw(es, DEST_REG, temp); if (cpu_state.abrt) return 1;       \
                                                                                \
                if (flags & D_FLAG) { DEST_REG -= 2; SRC_REG -= 2; }            \
                else                { DEST_REG += 2; SRC_REG += 2; }            \
                CNT_REG--;                                                      \
                cycles -= is486 ? 3 : 4;                                        \
                ins++;                                                          \
                reads++; writes++; total_cycles += is486 ? 3 : 4;               \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        ins--;                                                                  \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);              \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_MOVSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, writes = 0, total_cycles = 0;                            \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                uint32_t temp;                                                  \
                                                                                \
                CHECK_WRITE_REP(&_es, DEST_REG, DEST_REG);                      \
                temp = readmeml(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;    \
                writememl(es, DEST_REG, temp); if (cpu_state.abrt) return 1;       \
                                                                                \
                if (flags & D_FLAG) { DEST_REG -= 4; SRC_REG -= 4; }            \
                else                { DEST_REG += 4; SRC_REG += 4; }            \
                CNT_REG--;                                                      \
                cycles -= is486 ? 3 : 4;                                        \
                ins++;                                                          \
                reads++; writes++; total_cycles += is486 ? 3 : 4;               \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        ins--;                                                                  \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);              \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
                                                                                \
static int opREP_STOSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int writes = 0, total_cycles = 0;                                       \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                CHECK_WRITE_REP(&_es, DEST_REG, DEST_REG);                      \
                writememb(es, DEST_REG, AL); if (cpu_state.abrt) return 1;         \
                if (flags & D_FLAG) DEST_REG--;                                 \
                else                DEST_REG++;                                 \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                writes++; total_cycles += is486 ? 4 : 5;                        \
                ins++;                                                          \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, 0, 0, writes, 0, 0);                  \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_STOSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int writes = 0, total_cycles = 0;                                       \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                CHECK_WRITE_REP(&_es, DEST_REG, DEST_REG+1);                    \
                writememw(es, DEST_REG, AX); if (cpu_state.abrt) return 1;         \
                if (flags & D_FLAG) DEST_REG -= 2;                              \
                else                DEST_REG += 2;                              \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                writes++; total_cycles += is486 ? 4 : 5;                        \
                ins++;                                                          \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, 0, 0, writes, 0, 0);                  \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_STOSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int writes = 0, total_cycles = 0;                                       \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                CHECK_WRITE_REP(&_es, DEST_REG, DEST_REG+3);                    \
                writememl(es, DEST_REG, EAX); if (cpu_state.abrt) return 1;        \
                if (flags & D_FLAG) DEST_REG -= 4;                              \
                else                DEST_REG += 4;                              \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                writes++; total_cycles += is486 ? 4 : 5;                        \
                ins++;                                                          \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, 0, 0, 0, writes, 0);                  \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
static int opREP_LODSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, total_cycles = 0;                                        \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                AL = readmemb(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;      \
                if (flags & D_FLAG) SRC_REG--;                                  \
                else                SRC_REG++;                                  \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                reads++; total_cycles += is486 ? 4 : 5;                         \
                ins++;                                                          \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                   \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_LODSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, total_cycles = 0;                                        \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                AX = readmemw(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;      \
                if (flags & D_FLAG) SRC_REG -= 2;                               \
                else                SRC_REG += 2;                               \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                reads++; total_cycles += is486 ? 4 : 5;                         \
                ins++;                                                          \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                   \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_LODSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, total_cycles = 0;                                        \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                EAX = readmeml(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;     \
                if (flags & D_FLAG) SRC_REG -= 4;                               \
                else                SRC_REG += 4;                               \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                reads++; total_cycles += is486 ? 4 : 5;                         \
                ins++;                                                          \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, 0, reads, 0, 0, 0);                   \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \


#define REP_OPS_CMPS_SCAS(size, CNT_REG, SRC_REG, DEST_REG, FV) \
static int opREP_CMPSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, total_cycles = 0, tempz;                                 \
                                                                                \
        tempz = FV;                                                             \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                uint8_t temp = readmemb(cpu_state.ea_seg->base, SRC_REG);       \
                uint8_t temp2 = readmemb(es, DEST_REG); if (cpu_state.abrt) return 1;      \
                                                                                \
                if (flags & D_FLAG) { DEST_REG--; SRC_REG--; }                  \
                else                { DEST_REG++; SRC_REG++; }                  \
                CNT_REG--;                                                      \
                cycles -= is486 ? 7 : 9;                                        \
                reads += 2; total_cycles += is486 ? 7 : 9;                      \
                setsub8(temp, temp2);                                           \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                   \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_CMPSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, total_cycles = 0, tempz;                                 \
                                                                                \
        tempz = FV;                                                             \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                uint16_t temp = readmemw(cpu_state.ea_seg->base, SRC_REG);      \
                uint16_t temp2 = readmemw(es, DEST_REG); if (cpu_state.abrt) return 1;     \
                                                                                \
                if (flags & D_FLAG) { DEST_REG -= 2; SRC_REG -= 2; }            \
                else                { DEST_REG += 2; SRC_REG += 2; }            \
                CNT_REG--;                                                      \
                cycles -= is486 ? 7 : 9;                                        \
                reads += 2; total_cycles += is486 ? 7 : 9;                      \
                setsub16(temp, temp2);                                          \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                   \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_CMPSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0,  total_cycles = 0, tempz;                                \
                                                                                \
        tempz = FV;                                                             \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                uint32_t temp = readmeml(cpu_state.ea_seg->base, SRC_REG);      \
                uint32_t temp2 = readmeml(es, DEST_REG); if (cpu_state.abrt) return 1;     \
                                                                                \
                if (flags & D_FLAG) { DEST_REG -= 4; SRC_REG -= 4; }            \
                else                { DEST_REG += 4; SRC_REG += 4; }            \
                CNT_REG--;                                                      \
                cycles -= is486 ? 7 : 9;                                        \
                reads += 2; total_cycles += is486 ? 7 : 9;                      \
                setsub32(temp, temp2);                                          \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
        }                                                                       \
        PREFETCH_RUN(total_cycles, 1, -1, 0, reads, 0, 0, 0);                   \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
static int opREP_SCASB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, total_cycles = 0, tempz;                                 \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        tempz = FV;                                                             \
        while ((CNT_REG > 0) && (FV == tempz))                                  \
        {                                                                       \
                uint8_t temp = readmemb(es, DEST_REG); if (cpu_state.abrt) break;\
                setsub8(AL, temp);                                              \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
                if (flags & D_FLAG) DEST_REG--;                                 \
                else                DEST_REG++;                                 \
                CNT_REG--;                                                      \
                cycles -= is486 ? 5 : 8;                                        \
                reads++; total_cycles += is486 ? 5 : 8;                         \
                ins++;                                                          \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        ins--;                                                                  \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                   \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_SCASW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, total_cycles = 0, tempz;                                 \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        tempz = FV;                                                             \
        while ((CNT_REG > 0) && (FV == tempz))                                  \
        {                                                                       \
                uint16_t temp = readmemw(es, DEST_REG); if (cpu_state.abrt) break;\
                setsub16(AX, temp);                                             \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
                if (flags & D_FLAG) DEST_REG -= 2;                              \
                else                DEST_REG += 2;                              \
                CNT_REG--;                                                      \
                cycles -= is486 ? 5 : 8;                                        \
                reads++; total_cycles += is486 ? 5 : 8;                         \
                ins++;                                                          \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        ins--;                                                                  \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                   \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_SCASL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int reads = 0, total_cycles = 0, tempz;                                 \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        tempz = FV;                                                             \
        while ((CNT_REG > 0) && (FV == tempz))                                  \
        {                                                                       \
                uint32_t temp = readmeml(es, DEST_REG); if (cpu_state.abrt) break;\
                setsub32(EAX, temp);                                            \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
                if (flags & D_FLAG) DEST_REG -= 4;                              \
                else                DEST_REG += 4;                              \
                CNT_REG--;                                                      \
                cycles -= is486 ? 5 : 8;                                        \
                reads++; total_cycles += is486 ? 5 : 8;                         \
                ins++;                                                          \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        ins--;                                                                  \
        PREFETCH_RUN(total_cycles, 1, -1, 0, reads, 0, 0, 0);                   \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}

REP_OPS(a16, CX, SI, DI)
REP_OPS(a32, ECX, ESI, EDI)
REP_OPS_CMPS_SCAS(a16_NE, CX, SI, DI, 0)
REP_OPS_CMPS_SCAS(a16_E,  CX, SI, DI, 1)
REP_OPS_CMPS_SCAS(a32_NE, ECX, ESI, EDI, 0)
REP_OPS_CMPS_SCAS(a32_E,  ECX, ESI, EDI, 1)

static int opREPNE(uint32_t fetchdat)
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        CLOCK_CYCLES(2);
        PREFETCH_PREFIX();
        if (x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32])
                return x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
}
static int opREPE(uint32_t fetchdat)
{       
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        CLOCK_CYCLES(2);
        PREFETCH_PREFIX();
        if (x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32])
                return x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
}
